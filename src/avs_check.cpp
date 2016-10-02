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

//Validation
#if (defined(_DEBUG) && defined(NDEBUG)) || ((!defined(_DEBUG)) && (!defined(NDEBUG)))
#error Inconsistent debug flags!
#endif

//Debug
#ifndef NDEBUG
#define ENABLE_DEBUG 1
#else
#define ENABLE_DEBUG 0
#endif

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

//Const
#define DBL_NAN (std::numeric_limits<double>::quiet_NaN())

//Exit codes
static const int EXIT_LOAD_LIBRARY_FAILED    = 1;
static const int EXIT_DERTERMINE_PATH_FAILED = 2;
static const int EXIT_RESOLVE_ENTRYPT_FAILED = 3;
static const int EXIT_CHECK_VERSION_FAILED   = 4;

//=============================================================================
// UTILITY FUNCTIONS
//=============================================================================

class Library
{
public:
	Library(const wchar_t *name)
	{
		m_library = LoadLibraryW(name);
		m_error = m_library ? ERROR_SUCCESS : GetLastError();
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

	inline DWORD getError(void)
	{
		return m_error;
	}

	inline bool getPath(wchar_t *const path, const DWORD length)
	{
		if(!isNull())
		{
			const DWORD ret = GetModuleFileNameW(m_library, path, length);
			m_error = (ret != 0) ? ERROR_SUCCESS : GetLastError();
			return (ret > 0) && (ret < length);
		}
		m_error = ERROR_INVALID_FUNCTION;
		return false;
	}

	inline void* resolve(const char *const name)
	{
		if(!isNull())
		{
			void *const ret = GetProcAddress(m_library, name);
			m_error = (ret) ? ERROR_SUCCESS : GetLastError();
			return ret;
		}
		m_error = ERROR_INVALID_FUNCTION;
		return NULL;
	}

private:
	HMODULE m_library;
	DWORD m_error;
};

#define PRINT_ERROR_MSG(CODE) \
do \
{ \
	if (const wchar_t *const _msg_buffer = get_error_string((CODE))) \
	{ \
		fwprintf(stderr, L"%s\n\n", _msg_buffer); \
		free((void*)_msg_buffer); \
	} \
} \
while(0)

#define INIT_FUNCTION(LIB, NAME) NAME##_func NAME##_ptr; \
do \
{ \
	NAME##_ptr = (NAME##_func) (LIB).resolve(#NAME); \
	if(NAME##_ptr == NULL) \
	{ \
		const DWORD _errorCode = (LIB).getError(); \
		fwprintf(stderr, L"\nERROR: Function '%S' could not be resolved! [0x%X]\n\n", #NAME, _errorCode); \
		PRINT_ERROR_MSG(_errorCode); \
		return EXIT_RESOLVE_ENTRYPT_FAILED; \
	} \
} \
while(0)

static void fatal_exit(const wchar_t *const message)
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
			TerminateProcess(GetCurrentProcess(), UINT(-1));
		}
	}
}

static const wchar_t *get_error_string(const DWORD code)
{
	const wchar_t *buffer = NULL, *result = NULL;
	const size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buffer, 0, NULL);
	if (buffer)
	{
		if (size > 0)
		{
			result = _wcsdup(buffer);
		}
		LocalFree((HLOCAL)buffer);
	}
	return result;
}

static void remove_ucn_prefix(wchar_t *filePath)
{
	if(wcslen(filePath) >= 6)
	{
		if((_wcsnicmp(filePath, L"\\\\?\\", 4) == 0) && iswalpha(filePath[4]) && (filePath[5] == L':'))
		{
			wchar_t *src = &filePath[4];
			wchar_t *dst = &filePath[0];
			for(;;)
			{
				*dst = *src;
				if(!(*src))
				{
					break;
				}
				dst++; src++;
			}
		}
	}
}

static bool get_real_filename(const wchar_t *const virtual_path, wchar_t *const real_path, const DWORD length)
{
	typedef DWORD (__stdcall *GetFinalPathNameByHandleFunc)(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags);
	Library kernel32(L"kernel32");
	GetFinalPathNameByHandleFunc GetFinalPathNameByHandlePtr = (GetFinalPathNameByHandleFunc) kernel32.resolve("GetFinalPathNameByHandleW");
	if(!GetFinalPathNameByHandlePtr)
	{
		return false;
	}

	const HANDLE hFile = CreateFileW(virtual_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if((hFile == INVALID_HANDLE_VALUE) || (hFile == NULL))
	{
		return false;
	}

	const DWORD ret = GetFinalPathNameByHandlePtr(hFile, real_path, length, 0);
	if((length > 0) && (ret < length))
	{
		remove_ucn_prefix(real_path);
		return true;
	}

	return false;
}

//=============================================================================
// MAIN FUNCTION
//=============================================================================

static int check_avs_helper(void)
{
	//Load Avisynth DLL
	Library avisynthLib(L"avisynth");
	if(avisynthLib.isNull())
	{
		const DWORD errorCode = avisynthLib.getError();
		fwprintf(stderr, L"ERROR: Avisynth DLL could not be loaded! [0x%X]\n\n", errorCode);
		PRINT_ERROR_MSG(errorCode);
		return EXIT_LOAD_LIBRARY_FAILED;
	}

	//Determine Avisynth DLL Path
	wchar_t avisynthPath[4096];
	if(!avisynthLib.getPath(avisynthPath, 4096))
	{
		const DWORD errorCode = avisynthLib.getError();
		fwprintf(stderr, L"ERROR: Failed to determine Avisynth DLL path! [0x%X]\n\n", errorCode);
		PRINT_ERROR_MSG(errorCode);
		return EXIT_DERTERMINE_PATH_FAILED;
	}

	//Determine the *real* Avisynth Path
	wchar_t avisynthRealPath[4096];
	if(get_real_filename(avisynthPath, avisynthRealPath, 4096))
	{
		fwprintf(stderr, L"Avisynth_DLLPath=%s\n", avisynthRealPath);
		fflush(stderr);
	}
	else
	{
		fwprintf(stderr, L"Avisynth_DLLPath=%s\n", avisynthPath);
		fflush(stderr);
	}

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
		return EXIT_CHECK_VERSION_FAILED;
	}

	//Print Avisynth Version:
	fwprintf(stderr, L"Avisynth_Version=%.2f\n\n", avisynthVersion);
	fflush(stderr);

	//Clean up!
	return EXIT_SUCCESS;
}

static int check_avs(void)
{
	_setmode(_fileno(stderr), _O_U8TEXT);

	//Print logo
	fwprintf(stderr, L"Avisynth Checker %s [%S]\n", ARCH_NAME, __DATE__);
	fwprintf(stderr, L"Copyright (c) 2014-2015 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.\n\n");
	fwprintf(stderr, L"This program is free software: you can redistribute it and/or modify\n");
	fwprintf(stderr, L"it under the terms of the GNU General Public License <http://www.gnu.org/>.\n");
	fwprintf(stderr, L"Note that this program is distributed with ABSOLUTELY NO WARRANTY.\n\n");
	fflush(stderr);

	//Dump environemnt variable PATH (for DEBUG)
	if (ENABLE_DEBUG)
	{
		size_t required = 0;
		const errno_t error = _wgetenv_s(&required, NULL, 0, L"PATH");
		if (((!error) || (error == ERANGE)) && (required > 0))
		{
			const size_t buffSize = required;
			wchar_t *const buffer = (wchar_t*)malloc(sizeof(wchar_t) * buffSize);
			if (buffer)
			{
				if (_wgetenv_s(&required, buffer, buffSize, L"PATH") == 0)
				{
					fwprintf(stderr, L"PATH: %s\n\n", buffer);
				}
				free(buffer);
			}
		}
	}

	//Check for Avisynth
	const int result = check_avs_helper();
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

static int main_ex(void)
{
	try
	{
		return check_avs();
	}
	catch(...)
	{
		fatal_exit(L"\nFATAL ERROR: Unhandeled C++ exception!\n\n");
		return -1;
	}
}

int wmain(int argc, wchar_t* argv[])
{
#if(!(ENABLE_DEBUG))
	__try
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
		SetUnhandledExceptionFilter(my_exception_handler);
		_set_invalid_parameter_handler(my_invalid_param_handler);
		SetDllDirectoryW(L""); /*don'tload DLL from "current" directory*/
		return main_ex();
	}
	__except (1)
	{
		fatal_exit(L"\nFATAL ERROR: Unhandeled structured exception!\n\n");
		return -1;
	}
#else
	SetDllDirectoryW(L""); /*don'tload DLL from "current" directory*/
	return check_avs();
#endif
}

