/*
-------------------------------------------------------------------------------
>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> Vector4 Studios <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

Application	:		Quasar Engine

Author		:		Rounak Paul
Email		:		paulrounak1999@gmail.com
Date		:		4th Dec 2023

Description	:		Defination for Quasar API import/export
-------------------------------------------------------------------------------
*/

#pragma once

#ifdef QS_PLATFORM_WINDOWS
	#ifdef QS_BUILD_DLL 
		#define QUASAR_API __declspec(dllexport) 
	#else 
		#define QUASAR_API __declspec(dllimport) 
	#endif
#else 
	#error Quasar only supports Windows!
#endif
