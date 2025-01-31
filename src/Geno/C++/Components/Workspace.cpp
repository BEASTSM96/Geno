/*
 * Copyright (c) 2021 Sebastian Kylander https://gaztin.com/
 *
 * This software is provided 'as-is', without any express or implied warranty. In no event will
 * the authors be held liable for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose, including commercial
 * applications, and to alter it and redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not claim that you wrote the
 *    original software. If you use this software in a product, an acknowledgment in the product
 *    documentation would be appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be misrepresented as
 *    being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#include "Workspace.h"

#include "Compilers/CompilerGCC.h"
#include "Compilers/CompilerMSVC.h"
#include "GUI/Widgets/StatusBar.h"

#include <iostream>

#include <Common/Async/JobSystem.h>
#include <GCL/Deserializer.h>
#include <GCL/Serializer.h>

//////////////////////////////////////////////////////////////////////////

Workspace::Workspace( std::filesystem::path Location )
	: m_Location( std::move( Location ) )
	, m_Name    ( "MyWorkspace" )
{
	m_AppProcess = std::make_unique<Process>();
} // Workspace

//////////////////////////////////////////////////////////////////////////

void Workspace::Build( void )
{
	if( !m_Projects.empty() )
	{
		UTF8Converter                            UTF8Converter;
		std::vector< JobSystem::JobPtr >         LinkerJobs;
		std::vector< std::string >               LinkerJobProjectNames;
		std::shared_ptr< std::filesystem::path > LinkerOutput = std::make_shared< std::filesystem::path >();

		// Sort projects so that the link jobs exist to be depended upon
		std::vector< std::reference_wrapper< Project > > ProjectRefs;
		std::copy( m_Projects.begin(), m_Projects.end(), std::back_inserter( ProjectRefs ) );
		std::sort( ProjectRefs.begin(), ProjectRefs.end(), []( const Project& rA, const Project& rB )
			{
				auto& rLibraries = rB.m_LocalConfiguration.m_Libraries;
				return std::find( rLibraries.begin(), rLibraries.end(), rA.m_Name ) != rLibraries.end();
			}
		);


		for( Project& rProject : ProjectRefs )
		{
			//std::cout << "=== Building Project: " << rProject.m_Name << " ===\n";

			Configuration                                           Configuration = m_BuildMatrix.CurrentConfiguration();
			std::vector< std::shared_ptr< std::filesystem::path > > CompilerOutputs;

			rProject.m_LocalConfiguration.Override( Configuration );

			// Build Project
			rProject.Build();

			// Override the workspace config, as the project config could of changed
			Configuration.Override( rProject.m_LocalConfiguration );

			// Set project compiler outputs
			CompilerOutputs = rProject.CompilerOutputs();

			// Assemble a list of link jobs for projects that this depends on
			for( std::string& rLibrary : Configuration.m_Libraries )
			{
				auto Name = std::find( LinkerJobProjectNames.begin(), LinkerJobProjectNames.end(), rLibrary );
				if( Name != LinkerJobProjectNames.end() )
					rProject.LinkerDependencies().push_back( *std::next( LinkerJobs.begin(), std::distance( LinkerJobProjectNames.begin(), Name ) ) );
			}

			const std::wstring  ProjectName = UTF8Converter.from_bytes( rProject.m_Name );
			const Project::Kind Kind        = rProject.m_Kind;

			LinkerJobProjectNames.push_back( rProject.m_Name );

			// Push a new job with the projects link job and linker dependencies
			LinkerJobs.push_back( JobSystem::Instance().NewJob(
				[ Configuration, ProjectName, Kind, CompilerOutputs, LinkerOutput ]( void )
				{
					std::vector< std::filesystem::path > InputFiles;

					for( auto& rInputFile : CompilerOutputs )
						if( !rInputFile->empty() )
							InputFiles.emplace_back( std::move( *rInputFile ) );

					if( !InputFiles.empty() )
					{
						if( auto Result = Configuration.m_Compiler->Link( Configuration, InputFiles, ProjectName, Kind ) )
							*LinkerOutput = *Result;
					}
				},
				rProject.LinkerDependencies()
			) );

			//std::cout << "=== Done building '" << rProject.m_Name << "' Output -> "  << Configuration.m_Compiler->GetLinkerOutputPath( Configuration, ProjectName, rProject.m_Kind ).string() << " === \n";
		}

		JobSystem::Instance().NewJob(
			[ this, LinkerJobs, LinkerOutput ]( void )
			{
				if( auto& rLinkerOutput = *LinkerOutput; !rLinkerOutput.empty() )
				{
					std::cout << "Done building workspace\n";

					Events.BuildFinished( *this, rLinkerOutput, true );
				}
				else
				{
					std::cout << "Failed to build workspace\n";

					Events.BuildFinished( *this, "", false );
				}
			},
			LinkerJobs
		);
	}

} // Build

//////////////////////////////////////////////////////////////////////////

bool Workspace::Serialize( void )
{
	if( m_Location.empty() )
		return false;

	GCL::Serializer Serializer( ( m_Location / m_Name ).replace_extension( EXTENSION ) );
	if( !Serializer.IsOpen() )
		return false;

	// Name string
	{
		GCL::Object Name( "Name" );
		Name.SetString( m_Name );

		Serializer.WriteObject( Name );
	}

	// Matrix table
	{
		GCL::Object Matrix( "Matrix", std::in_place_type< GCL::Object::TableType > );

		for( const BuildMatrix::Column& column : m_BuildMatrix.m_Columns )
		{
			SerializeBuildMatrixColumn( Matrix, column );
		}

		Serializer.WriteObject( Matrix );
	}

	// Projects array
	{
		GCL::Object Projects( "Projects", std::in_place_type< GCL::Object::TableType > );

		for( Project& rProject : m_Projects )
		{
			std::filesystem::path relative_project_path = rProject.m_Location.lexically_relative( m_Location ) / rProject.m_Name;

			Projects.AddChild( GCL::Object( relative_project_path.string() ) );

			rProject.Serialize();
		}

		Serializer.WriteObject( Projects );
	}

	return true;

} // Serialize

//////////////////////////////////////////////////////////////////////////

bool Workspace::Deserialize( void )
{
	if( m_Location.empty() )
		return false;

	GCL::Deserializer Serializer( ( m_Location / m_Name ).replace_extension( EXTENSION ) );
	if( !Serializer.IsOpen() )
		return false;

	Serializer.Objects( this, GCLObjectCallback );

	return true;

} // Deserialize

//////////////////////////////////////////////////////////////////////////

void Workspace::Rename( std::string Name )
{
	const std::filesystem::path OldPath = ( m_Location / m_Name ).replace_extension( EXTENSION );

	if( std::filesystem::exists( OldPath ) )
	{
		const std::filesystem::path NewPath = ( m_Location / Name ).replace_extension( EXTENSION );
		std::filesystem::rename( OldPath, NewPath );
	}

	m_Name = std::move( Name );
	Serialize();

} // Rename

//////////////////////////////////////////////////////////////////////////

Project& Workspace::NewProject( std::filesystem::path Location, std::string Name )
{
	Project& project = m_Projects.emplace_back( std::move( Location ) );
	project.m_Name   = std::move( Name );

	return project;

} // NewProject

//////////////////////////////////////////////////////////////////////////

Project* Workspace::ProjectByName( std::string_view Name )
{
	for( Project& rProject : m_Projects )
	{
		if( rProject.m_Name == Name )
			return &rProject;
	}

	return nullptr;

} // ProjectByName

//////////////////////////////////////////////////////////////////////////

bool Workspace::AddProject( const std::filesystem::path& rPath )
{
	Project* pProject = ProjectByName( rPath.stem().string() );
	if( !pProject )
	{
		std::filesystem::path ProjectPath = rPath;
		ProjectPath                       = ProjectPath.lexically_normal();

		Project& rProject = NewProject( ProjectPath.parent_path(), ProjectPath.stem().string() );
		if( rProject.Deserialize() )
			return true;
	}
	return false;

} // AddProject

//////////////////////////////////////////////////////////////////////////

void Workspace::RemoveProject( const std::string& rName )
{
	if( ProjectByName( rName ) )
	{
		for( auto It = m_Projects.begin(); It != m_Projects.end(); ++It )
		{
			if( It->m_Name == rName )
			{
				m_Projects.erase( It );
				Serialize();
				break;
			}
		}
	}

} // RemoveProject

//////////////////////////////////////////////////////////////////////////

void Workspace::RenameProject( const std::string& rProjectName, std::string Name )
{
	if( Project* pProject = ProjectByName( rProjectName ) )
	{
		const std::filesystem::path OldPath = ( pProject->m_Location / pProject->m_Name ).replace_extension( Project::EXTENSION );

		if( std::filesystem::exists( OldPath ) )
		{
			const std::filesystem::path NewPath = ( pProject->m_Location / Name ).replace_extension( Project::EXTENSION );
			std::filesystem::rename( OldPath, NewPath );
		}

		pProject->m_Name = std::move( Name );
		pProject->Serialize();
		Serialize();
	}

} // RenameProject

//////////////////////////////////////////////////////////////////////////

void Workspace::GCLObjectCallback( GCL::Object pObject, void* pUser )
{
	Workspace*       pSelf = ( Workspace* )pUser;
	std::string_view Name  = pObject.Name();

	if( Name == "Name" )
	{
		pSelf->m_Name = pObject.String();
	}
	else if( Name == "Matrix" )
	{
		pSelf->m_BuildMatrix = BuildMatrix();

		for( const GCL::Object& rColumn : pObject.Table() )
		{
			const std::string_view ColumnName = rColumn.Name();

			pSelf->m_BuildMatrix.NewColumn( std::string( ColumnName ) );
			pSelf->DeserializeBuildMatrixColumn( pSelf->m_BuildMatrix.m_Columns.back(), rColumn );
		}
	}
	else if( Name == "Projects" )
	{
		for( const GCL::Object& rProjectPathObj : pObject.Table() )
		{
			std::filesystem::path ProjectPath = rProjectPathObj.String();

			if( !ProjectPath.is_absolute() )
				ProjectPath = pSelf->m_Location / ProjectPath;

			ProjectPath = ProjectPath.lexically_normal();

			Project& rProject = pSelf->NewProject( ProjectPath.parent_path(), ProjectPath.filename().string() );
			rProject.Deserialize();
		}
	}

} // GCLObjectCallback

//////////////////////////////////////////////////////////////////////////

void Workspace::SerializeBuildMatrixColumn( GCL::Object& rObject, const BuildMatrix::Column& rColumn )
{
	GCL::Object ColumnObj( rColumn.Name, std::in_place_type< GCL::Object::TableType > );

	for( const auto& [ rName, rConfiguration ] : rColumn.Configurations )
	{
		GCL::Object ConfigurationObj( rName );

		if( rConfiguration.m_Compiler || rConfiguration.m_Architecture || rConfiguration.m_Optimization )
		{
			GCL::Object::TableType& rTable = ConfigurationObj.SetTable();

			if( rConfiguration.m_Compiler )
				rTable.emplace_back( "Compiler" ).SetString( std::string( rConfiguration.m_Compiler->GetName() ) );

			if( rConfiguration.m_Architecture )
				rTable.emplace_back( "Architecture" ).SetString( std::string( Reflection::EnumToString( *rConfiguration.m_Architecture ) ) );

			if( rConfiguration.m_Optimization )
				rTable.emplace_back( "Optimization" ).SetString( std::string( Reflection::EnumToString( *rConfiguration.m_Optimization ) ) );
		}

		ColumnObj.AddChild( std::move( ConfigurationObj ) );
	}

	rObject.AddChild( std::move( ColumnObj ) );

} // SerializeBuildMatrixColumn

//////////////////////////////////////////////////////////////////////////

void Workspace::DeserializeBuildMatrixColumn( BuildMatrix::Column& rColumn, const GCL::Object& rObject )
{
	for( const GCL::Object& rConfigurationObj : rObject.Table() )
	{
		::Configuration Configuration;

		if( rConfigurationObj.IsTable() )
		{
			const GCL::Object::TableType& rTable = rConfigurationObj.Table();

			if( auto Compiler = std::find_if( rTable.begin(), rTable.end(), []( const GCL::Object& rObject ) { return rObject.Name() == "Compiler"; } )
			;   Compiler != rTable.end() && Compiler->IsString() )
			{
				const GCL::Object::StringType& rCompilerValue = Compiler->String();

				if( false );
#if defined( _WIN32 )
				else if( rCompilerValue == "MSVC" ) { Configuration.m_Compiler = std::make_shared< CompilerMSVC >(); }
#endif // _WIN32
				else if( rCompilerValue == "GCC" ) { Configuration.m_Compiler = std::make_shared< CompilerGCC >(); }
				else                               { std::cerr << "Unrecognized compiler '" << rCompilerValue << "' for this workspace.\n"; }
			}

			if( auto Architecture = std::find_if( rTable.begin(), rTable.end(), []( const GCL::Object& rObject ) { return rObject.Name() == "Architecture"; } )
			;   Architecture != rTable.end() && Architecture->IsString() )
			{
				const GCL::Object::StringType& rArchitecture = Architecture->String();

				Reflection::EnumFromString( rArchitecture, Configuration.m_Architecture.emplace() );
			}

			if( auto Optimization = std::find_if( rTable.begin(), rTable.end(), []( const GCL::Object& rObject ) { return rObject.Name() == "Optimization"; } )
			;   Optimization != rTable.end() && Optimization->IsString() )
			{
				const GCL::Object::StringType& rOptimization = Optimization->String();

				Reflection::EnumFromString( rOptimization, Configuration.m_Optimization.emplace() );
			}
		}

		rColumn.Configurations.emplace_back( rConfigurationObj.Name(), std::move( Configuration ) );
	}

} // DeserializeBuildMatrixColumn
