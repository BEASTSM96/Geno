/*
 * Copyright (c) 2020 Sebastian Kylander https://gaztin.com/
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

#include "OutputWindow.h"

#include <cassert>
#include <iostream>

#include <fcntl.h>
#include <io.h>

#include <imgui.h>

enum
{
	READ,
	WRITE,
};

constexpr uint32_t pipe_size = 65536;

OutputWindow::OutputWindow( void )
{
	if( ( stdout_ = _fileno( stdout ) ) < 0 )
	{
		if( FILE* f = nullptr; freopen_s( &f, "CONOUT$", "w", stdout ) == 0 && f != nullptr )
			stdout_ = _fileno( f );
	}
	
	if( ( stderr_ = _fileno( stderr ) ) < 0 )
	{
		if( FILE* f = nullptr; freopen_s( &f, "CONOUT$", "w", stderr ) == 0 && f != nullptr )
			stderr_ = _fileno( f );
	}

	// Need stdout and stderr
	assert( stdout_ > 0 );
	assert( stderr_ > 0 );

	// Make stdout and stderr unbuffered so that we don't need to fflush before and after capture
	setvbuf( stdout, nullptr, _IONBF, 0 );
	setvbuf( stderr, nullptr, _IONBF, 0 );

	// Duplicate stdout and stderr
	old_stdout_ = _dup( stdout_ );
	old_stderr_ = _dup( stderr_ );

	if( _pipe( pipe_, pipe_size, O_BINARY ) != -1 )
	{
		BeginCapture();

		std::cout << "stdout\n";
		std::cerr << "stderr\n";
	}
}

OutputWindow::OutputWindow( OutputWindow&& other )
{
	*this = std::move( other );
}

OutputWindow::~OutputWindow( void )
{
	EndCapture();

	if( old_stdout_ > 0 )
		_close( old_stdout_ );

	if( old_stderr_ > 0 )
		_close( old_stderr_ );

	if( pipe_[ READ ] > 0 )
		_close( pipe_[ READ ] );

	if( pipe_[ WRITE ] > 0 )
		_close( pipe_[ WRITE ] );
}

OutputWindow& OutputWindow::operator=( OutputWindow&& other )
{
	pipe_[ 0 ]  = other.pipe_[ 0 ];
	pipe_[ 1 ]  = other.pipe_[ 1 ];
	old_stdout_ = other.old_stdout_;
	old_stderr_ = other.old_stderr_;
	show_       = other.show_;
	captured_   = std::move( other.captured_ );

	other.pipe_[ 0 ]  = 0;
	other.pipe_[ 1 ]  = 0;
	other.old_stdout_ = 0;
	other.old_stderr_ = 0;
	other.show_       = false;

	return *this;
}

void OutputWindow::Show( void )
{
	if( ImGui::Begin( "Output", &show_ ) )
	{
		EndCapture();
		BeginCapture();

		ImGui::TextUnformatted( captured_.c_str(), captured_.c_str() + captured_.size() );
	}
	ImGui::End();
}

void OutputWindow::BeginCapture( void )
{
	if( int out = _fileno( stdout ); out > 0 )
		_dup2( pipe_[ WRITE ], out );

	if( int err = _fileno( stderr ); err > 0 )
		_dup2( pipe_[ WRITE ], err );
}

void OutputWindow::EndCapture( void )
{
	if( int out = _fileno( stdout ); out > 0 )
		_dup2( old_stdout_, out );

	if( int err = _fileno( stderr ); err > 0 )
		_dup2( old_stderr_, err );

	if( pipe_[ READ ] > 0 )
	{
		char   buf[ 1024 ];
		size_t bytes_read = 0;

		if( !_eof( pipe_[ READ ] ) )
		{
			memset( buf, 0, std::size( buf ) );
			bytes_read = _read( pipe_[ READ ], buf, std::size( buf ) );
			captured_ += buf;
		}

		while( bytes_read == std::size( buf ) )
		{
			bytes_read = 0;

			if( !_eof( pipe_[ READ ] ) )
			{
				memset( buf, 0, std::size( buf ) );
				bytes_read = _read( pipe_[ READ ], buf, std::size( buf ) );
				captured_ += buf;
			}
		}
	}
}
