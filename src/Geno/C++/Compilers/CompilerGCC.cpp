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

#include "CompilerGCC.h"

#include <Common/Process.h>

#include <filesystem>

//////////////////////////////////////////////////////////////////////////

static int FindGCCVersion()
{
	// Run GCC with --version to get the version number of GCC
	int                Result = 0;
	Process            GCCVersionProcess = Process( L"g++ --version" );

	Result = GCCVersionProcess.ResultOf();

	if ( Result > 0 )
		return Result;

	return 0;
}

//////////////////////////////////////////////////////////////////////////

static std::wstring FindSTLIncludeDirs()
{
	// Find standard library files on Linux.

	std::filesystem::path STLPath;

	// TODO: Support MacOS and Support other Linux distros that don't use this file structure.
#if defined( __linux__ )

	STLPath = "/usr/include/c++";
	STLPath / std::to_string( FindGCCVersion() );

	if( !std::filesystem::exists( STLPath ) )
		return L"";

#endif // __linux__

	return STLPath;
}

//////////////////////////////////////////////////////////////////////////

std::wstring CompilerGCC::MakeCompilerCommandLineString( const Configuration& rConfiguration, const std::filesystem::path& rFilePath )
{
	std::wstring Command;
	Command.reserve( 1024 );

	// Start with G++ executable
	Command += L"g++";

	// Make it so that we compile separately.
	Command += L" -c";

	// Language
	const auto FileExtension = rFilePath.extension();
	if     ( FileExtension == ".c"   ) Command += L" -x c";
	else if( FileExtension == ".cpp" ) Command += L" -x c++";
	else if( FileExtension == ".cxx" ) Command += L" -x c++";
	else if( FileExtension == ".cc"  ) Command += L" -x c++";
	else if( FileExtension == ".asm" ) Command += L" -x assembler";
	else                               Command += L" -x none";

	// Defines
	// Start by walking-through the user defines

	for( const std::string& rDefine : rConfiguration.m_Defines )
	{
		Command += L"-D " + ( wchar_t )rDefine.c_str();
	}

	// Includes
	// Start by including the standard library headers and any POSIX API files.

	{
		for ( const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator( FindSTLIncludeDirs() ) )
		{
			const std::filesystem::path DirectoryPath = rEntry.path();

			Command +=  L"-include " + DirectoryPath.wstring();
		}

		// C Lib and POSIX
		for( const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator( "/usr/include" ) )
		{
			const std::filesystem::path DirectoryPath = rEntry.path();

			Command +=  L"-include " + DirectoryPath.wstring();
		}

	}

	for ( const std::filesystem::path& rIncludePath : rConfiguration.m_IncludeDirs )
	{
		Command +=  L"-include " + rIncludePath.wstring();
	}

	// Verbosity
	if( rConfiguration.m_Verbose )
	{
		// Time the execution of each subprocess
		Command += L" -time";

		// Verbose logging
		Command += L" -v";
	}

	// Set output file
	Command += L" -o " + GetCompilerOutputPath( rConfiguration, rFilePath ).wstring();

	// Finally, the input source file
	Command += L" " + rFilePath.wstring();

	return Command;

} // MakeCompilerCommandLineString

//////////////////////////////////////////////////////////////////////////

std::wstring CompilerGCC::MakeLinkerCommandLineString( const Configuration& rConfiguration, std::span< std::filesystem::path > InputFiles, const std::wstring& rOutputName, Project::Kind Kind )
{
	std::wstring Command;
	Command.reserve( 256 );

	UTF8Converter UTF8;

	switch( Kind )
	{
		case Project::Kind::Application:
		case Project::Kind::DynamicLibrary:
		{
			// Start with G++ executable
			Command += L"g++";

			// Create a shared library
			if( Kind == Project::Kind::DynamicLibrary )
				Command += L" -shared";

			// User-defined library directories
			for( const std::filesystem::path& rLibraryDirectory : rConfiguration.m_LibraryDirs )
			{
				Command += L" -L" + rLibraryDirectory.wstring();
			}

			// Link libraries
			for( const std::string& rLibrary : rConfiguration.m_Libraries )
			{
				Command += L" -l" + UTF8.from_bytes( rLibrary );
			}

			// Set output file
			Command += L" -o " + GetLinkerOutputPath( rConfiguration, rOutputName, Kind ).wstring();

			// Finally, set the object files
			for( const std::filesystem::path& rInputFile : InputFiles )
				Command += L" " + rInputFile.wstring();

		} break;

		case Project::Kind::StaticLibrary:
		{
			// Start with AR executable
			Command += L"bin/ar";

			// Command: Replace existing or insert new file(s) into the archive
			Command += L" r";

			// Use full path names when matching
			Command += L'P';

			// Only replace files that are newer than current archive contents
			Command += L'u';

			// Do not warn if the library had to be created
			Command += L'c';

			// Create an archive index (cf. ranlib)
			Command += L's';

			// Set output file
			Command += L" " + GetLinkerOutputPath( rConfiguration, rOutputName, Kind ).wstring();

			// Set input files
			for( const std::filesystem::path& rInputFile : InputFiles )
				Command += L" " + rInputFile.wstring();

		} break;

		default:
		{
		} break;
	}

	return Command;

} // MakeLinkerCommandLineString
