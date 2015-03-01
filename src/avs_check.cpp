///////////////////////////////////////////////////////////////////////////////
// Avisynth Checker
// Copyright (C) 2014-2015 LoRd_MuldeR <MuldeR2@GMX.de>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// http://www.gnu.org/licenses/gpl-2.0.txt
///////////////////////////////////////////////////////////////////////////////

//CRT
#include <cstdlib>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <limits>

//Windows
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

//Avisynth
#include "avisynth_c.h"

//Platform
#if defined(_M_X64)
static const wchar_t *ARCH_NAME = L"x64";
#else
static const wchar_t *ARCH_NAME = L"x86";
#endif

#define DBL_NAN (std::numeric_limits<double>::quiet_NaN())

//=============================================================================
// UTILITY FUNCTIONS
//=============================================================================

class Library
{
public:
	Library(const wchar_t *name)
	{
		m_library = LoadLibraryW(name);
	}
	~Library()
	{
		if(!isNull())
		{
			FreeLibrary(m_library);
		}
	}
	inline bool isNull(void)
	{
		return (m_library == NULL);
	}
	inline bool getPath(wchar_t *path, const DWORD length)
	{
		if(!isNull())
		{
			const DWORD ret = GetModuleFileNameW(m_library, path, length);
			return (ret > 0) && (ret < length);
		}
		return false;
	}
	inline void* resolve(const char *name)
	{
		if(!isNull())
		{
			return GetProcAddress(m_library, name);
		}
		return NULL;
	}
private:
	HMODULE m_library;
};

#define INIT_FUNCTION(LIB, NAME) NAME##_func NAME##_ptr; \
do \
{ \
	NAME##_ptr = (NAME##_func) (LIB).resolve(#NAME); \
	if(NAME##_ptr == NULL) \
	{ \
		fwprintf(stderr, L"\nERROR: Function '%S' could not be resolved!\n\n", #NAME); \
		return false; \
	} \
} \
while(0)

static void fatal_exit(const wchar_t *message)
{
	for(;;)
	{
		__try
		{
			fwprintf(stderr, message);
			fflush(stderr);
		}
		__finally
		{
			TerminateProcess(GetCurrentProcess(), 666);
		}
	}
}

//=============================================================================
// MAIN FUNCTION
//=============================================================================

static bool check_avs_helper(void)
{
	//Load Avisynth DLL
	Library avisynthLib(L"avisynth");
	if(avisynthLib.isNull())
	{
		fwprintf(stderr, L"ERROR: Avisynth DLL could not be loaded!\n\n");
		return false;
	}

	//Determine Avisynth DLL Path
	wchar_t avisynthPath[4096];
	if(!avisynthLib.getPath(avisynthPath, 4096))
	{
		fwprintf(stderr, L"ERROR: Failed to determine Avisynth DLL path!\n\n");
		return false;
	}

	//Print path
	fwprintf(stderr, L"Avisynth_DLLPath=%s\n", avisynthPath);
	fflush(stderr);

	//Initialize Function Pointes
	INIT_FUNCTION(avisynthLib, avs_create_script_environment);
	INIT_FUNCTION(avisynthLib, avs_delete_script_environment);
	INIT_FUNCTION(avisynthLib, avs_invoke);
	INIT_FUNCTION(avisynthLib, avs_function_exists);
	INIT_FUNCTION(avisynthLib, avs_release_value);

	//Now try to determine Avisynth version
	double avisynthVersion = DBL_NAN;
	AVS_ScriptEnvironment* avs_env = avs_create_script_environment_ptr(AVS_INTERFACE_25);
	if(avs_env != NULL)
	{
		if(avs_function_exists_ptr(avs_env, "VersionNumber"))
		{
			AVS_Value avs_version = avs_invoke_ptr(avs_env, "VersionNumber", avs_new_value_array(NULL, 0), NULL);
			if(!avs_is_error(avs_version))
			{
				if(avs_is_float(avs_version))
				{
					avisynthVersion = avs_as_float(avs_version);
					if(avs_release_value_ptr) avs_release_value_ptr(avs_version);
				}
			}
		}
		avs_delete_script_environment_ptr(avs_env);
	}

	if(isnan(avisynthVersion) || (avisynthVersion < 2.5))
	{
		fwprintf(stderr, L"\nERROR: Failed to determine Avisynth version!\n\n");
		return false;
	}

	//Print Avisynth Version:
	fwprintf(stderr, L"Avisynth_Version=%.2f\n\n", avisynthVersion);
	fflush(stderr);

	//Clean up!
	return true;
}

static bool check_avs(void)
{
	_setmode(_fileno(stderr), _O_U8TEXT);

	//Print logo
	fwprintf(stderr, L"Avisynth Checker %s [%S]\n", ARCH_NAME, __DATE__);
	fwprintf(stderr, L"Copyright (c) 2014-2015 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.\n\n");
	fwprintf(stderr, L"This program is free software: you can redistribute it and/or modify\n");
	fwprintf(stderr, L"it under the terms of the GNU General Public License <http://www.gnu.org/>.\n");
	fwprintf(stderr, L"Note that this program is distributed with ABSOLUTELY NO WARRANTY.\n\n");
	fflush(stderr);

	//Check for Avisynth
	const bool result = check_avs_helper();
	if(result)
	{
		fwprintf(stderr, L"Avisynth v2.5+ (%s) is available on this machine :-)\n\n", ARCH_NAME);
		fflush(stderr);
	}
	else
	{
		fwprintf(stderr, L"Avisynth v2.5+ (%s) is *NOT* available on this machine :-(\n\n", ARCH_NAME);
		fflush(stderr);
	}

	return result;
}

//=============================================================================
// ERROR HANDLER
//=============================================================================

// Invalid parameters handler
static void my_invalid_param_handler(const wchar_t* exp, const wchar_t* fun, const wchar_t* fil, unsigned int, uintptr_t)
{
	fatal_exit(L"\nFATAL ERROR: Invalid parameter handler invoked!\n\n");
}

// Global exception handler
static LONG WINAPI my_exception_handler(struct _EXCEPTION_POINTERS *ExceptionInfo)
{
	fatal_exit(L"\nFATAL ERROR: Unhandeled exception handler invoked!\n\n");
	return LONG_MAX;
}

//=============================================================================
// ENTRY POINT
//=============================================================================

static bool main_ex(void)
{
	try
	{
		return check_avs();
	}
	catch(...)
	{
		fatal_exit(L"\nFATAL ERROR: Unhandeled C++ exception!\n\n");
		return false;
	}
}

int wmain(int argc, wchar_t* argv[])
{
	__try
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
		SetUnhandledExceptionFilter(my_exception_handler);
		_set_invalid_parameter_handler(my_invalid_param_handler);
		SetDllDirectoryW(L""); /*don'tload DLL from "current" directory*/
		return main_ex() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	__except(1)
	{
		fatal_exit(L"\nFATAL ERROR: Unhandeled structured exception!\n\n");
		return -1;
	}
}

