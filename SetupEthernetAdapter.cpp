#include "stdafx.h"

#include "SetupEthernetAdapter.h"

#include <wlanapi.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CWinApp theApp;

#define SNA_ERROR_GETMODULEHANDLE 1
#define SNA_ERROR_INITIALIZEMFC 2
#define SNA_ERROR_MALLOC 3
#define SNA_ERROR_GETADAPTERSADDRESS 4
#define SNA_ERROR_NOADAPTERFOUND 5
#define SNA_ERROR_CONVERTINTERFACEINDEXTOLUID 6
#define SNA_ERROR_CONVERTINTERFACELUIDTOALIAS 7

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "SHELL32.LIB") 

int SetupNetworkAdapter()
{
	CMapStringToString mapAdapters;

	// Get network adapters for IPv4 using GetAdaptersAddresses
	ULONG nFlags = GAA_FLAG_INCLUDE_PREFIX;
	ULONG nFamily = AF_INET;
	PIP_ADAPTER_ADDRESSES pAddresses = NULL;
	PIP_ADAPTER_ADDRESSES pCurrAddress = NULL;
	ULONG nBufLen = 16324, nTries = 0, nMaxTries = 3;
	DWORD dwResult;

	do
	{
		pAddresses = (IP_ADAPTER_ADDRESSES*) MALLOC(nBufLen);
		if (pAddresses == NULL)
		{
			_tprintf(_T("Memory allocation failed for IP_ADAPTER_ADDRESSES struct\n"));
			return SNA_ERROR_MALLOC;
		}

		dwResult = GetAdaptersAddresses(nFamily, nFlags, NULL, pAddresses, &nBufLen);
		if (dwResult == ERROR_BUFFER_OVERFLOW)
		{
			FREE(pAddresses);
			pAddresses = NULL;
		}
		else
			break;

		nTries++;
	}
	while (dwResult == ERROR_BUFFER_OVERFLOW && nTries < nMaxTries);

	if (dwResult != NO_ERROR)
	{
		_tprintf(_T("Call to GetAdaptersAddresses failed with error: %d\n"), dwResult);
		FREE(pAddresses);
		return SNA_ERROR_GETADAPTERSADDRESS;
	}

	pCurrAddress = pAddresses;
	while (pCurrAddress)
	{
		if (pCurrAddress->IfType == IF_TYPE_ETHERNET_CSMACD)
		{
			USES_CONVERSION;
			CString cKey = A2T(pCurrAddress->AdapterName);
			mapAdapters[cKey] = pCurrAddress->FriendlyName;
		}

		pCurrAddress = pCurrAddress->Next;
	}
	FREE(pAddresses);

	if (mapAdapters.GetCount() == 0)
	{
		_tprintf(_T("No ethernet adapter found\n"));
		return SNA_ERROR_NOADAPTERFOUND;
	}

	// On older OSes (Windows XP) wireless network cards are reported as IF_TYPE_ETHERNET_CSMACD
	OSVERSIONINFO osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osvi);
	if (osvi.dwMajorVersion < 6)
	{
		_tprintf(_T("Compatibility mode\n"));

		HANDLE hClient = NULL;
		DWORD dwMaxClient = 2;
		DWORD dwCurVersion = 0;
		PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
		PWLAN_INTERFACE_INFO pIfInfo = NULL;

		dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
		if (dwResult == ERROR_SUCCESS)
		{
			dwResult = WlanEnumInterfaces(hClient, NULL, &pIfList);
			if (dwResult == ERROR_SUCCESS)
			{
				WCHAR szWlanGuid[256] = { 0 };
				int nMaxLen = 256;

				for (int i = 0; i < (int) pIfList->dwNumberOfItems; i++)
				{
					pIfInfo = (WLAN_INTERFACE_INFO *) &pIfList->InterfaceInfo[i];
					if (StringFromGUID2(pIfInfo->InterfaceGuid, (LPOLESTR) szWlanGuid, nMaxLen) > 0)
					{
						CString cKey = szWlanGuid;
						CString cValue;
						if (mapAdapters.Lookup(cKey, cValue))
							mapAdapters.RemoveKey(szWlanGuid);
					}
				}
			}
			else
				_tprintf(_T("WlanEnumInterfaces failed with error: %u\n"), dwResult);

			WlanCloseHandle(hClient, NULL);
		}
		else
			_tprintf(_T("WlanOpenHandle failed with error: %u\n"), dwResult);
	}

	// Get network cards GUID from registry to give higher priority to physical adapters
	// We aim to avoid virtual devices
	CString sFriendlyName;
	CRegKey oRegKey;
	TCHAR szSubkey[1024];
	DWORD dwNameLength;
	CString sBaseKey = _T("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkCards");

	if (ERROR_SUCCESS == oRegKey.Open(HKEY_LOCAL_MACHINE, sBaseKey, KEY_READ | KEY_ENUMERATE_SUB_KEYS))
	{
		DWORD dwIndex = 0;
		while (TRUE)
		{
			dwNameLength = 1024;
			if (ERROR_NO_MORE_ITEMS == oRegKey.EnumKey(dwIndex++, szSubkey, &dwNameLength, NULL))
				break;

			CRegKey oRegKeyCard;
			if (ERROR_SUCCESS == oRegKeyCard.Open(HKEY_LOCAL_MACHINE, sBaseKey + _T("\\") + szSubkey, KEY_READ))
			{
				TCHAR szGuid[256];
				DWORD dwGuidLength = 256;
				CString cGuid;

				if (ERROR_SUCCESS == oRegKeyCard.QueryStringValue(_T("ServiceName"), szGuid, &dwGuidLength))
				{
					CString cValue;
					cGuid = szGuid;
					cGuid.Trim();
					if (mapAdapters.Lookup(cGuid, cValue))
						sFriendlyName = cValue;					
				}
				oRegKeyCard.Close();

				if (!sFriendlyName.IsEmpty())
					break;
			}
		}
		oRegKey.Close();
	}

	// No network adapter found in registry. Choose among the ones available
	if (sFriendlyName.IsEmpty())
	{
		CString sKey;
		POSITION pos = mapAdapters.GetStartPosition();
		if (pos != NULL)
			mapAdapters.GetNextAssoc(pos, sKey, sFriendlyName);
	}

	if (sFriendlyName.IsEmpty())
	{
		_tprintf(_T("No ethernet adapter found\n"));
		return SNA_ERROR_NOADAPTERFOUND;
	}

	// Build netsh commands
	TCHAR szTempFld[MAX_PATH];
	GetTempPath(MAX_PATH, szTempFld);
	CString sTempFld = szTempFld;

	CString sIp = _T("192.168.1.2");
	CString sMask = _T("255.255.255.0");
	CString sGateway = _T("192.168.1.1");
	CString sNetshIp;
	sNetshIp.Format(_T("netsh interface ip set address name=\"%s\" static %s %s %s"), sFriendlyName, sIp, sMask, sGateway);
	sNetshIp.Trim();

	CString sDns = _T("8.8.8.8");
	CString sNetshDns;
	sNetshDns.Format(_T("netsh interface ip set dns name=\"%s\" static %s"), sFriendlyName, sDns);

	// Set IP configuration of adapter
	_tprintf(_T("Running: %s\n"), sNetshIp);
	::ShellExecute(NULL, NULL, _T("cmd.exe"), CString(_T("/C ")) + sNetshIp, NULL, SW_HIDE);
	_tprintf(_T("Running: %s\n"), sNetshDns);
	::ShellExecute(NULL, NULL, _T("cmd.exe"), CString(_T("/C ")) + sNetshDns, NULL, SW_HIDE);

	return 0;
}

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	int nRetCode = 0;

	HMODULE hModule = ::GetModuleHandle(NULL);
	if (hModule != NULL)
	{
		if (!AfxWinInit(hModule, NULL, ::GetCommandLine(), 0))
		{
			_tprintf(_T("Unable to initialize MFC\n"));
			nRetCode = SNA_ERROR_INITIALIZEMFC;
		}
		else
			nRetCode = SetupNetworkAdapter();
	}
	else
	{
		_tprintf(_T("GetModuleHandle error\n"));
		nRetCode = SNA_ERROR_GETMODULEHANDLE;
	}

	return nRetCode;
}
