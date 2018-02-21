// Copyright (c) .NET Foundation. All rights reserved.
// Licensed under the MIT License. See License.txt in the project root for license information.

#pragma once

#define HOSTING_STARTUP_ASSEMBLIES_ENV_STR          L"ASPNETCORE_HOSTINGSTARTUPASSEMBLIES"
#define HOSTING_STARTUP_ASSEMBLIES_NAME             L"ASPNETCORE_HOSTINGSTARTUPASSEMBLIES="
#define HOSTING_STARTUP_ASSEMBLIES_VALUE            L"Microsoft.AspNetCore.Server.IISIntegration"
#define ASPNETCORE_IIS_AUTH_ENV_STR                 L"ASPNETCORE_IIS_HTTPAUTH="
#define ASPNETCORE_IIS_AUTH_WINDOWS                 L"windows;"
#define ASPNETCORE_IIS_AUTH_BASIC                   L"basic;"
#define ASPNETCORE_IIS_AUTH_ANONYMOUS               L"anonymous;"
#define ASPNETCORE_IIS_AUTH_NONE                    L"none"

//
// The key used for hash-table lookups, consists of the port on which the http process is created.
//

class ENVIRONMENT_VAR_ENTRY
{
public:
    ENVIRONMENT_VAR_ENTRY():
        _cRefs(1)
    {
    }

    HRESULT
    Initialize(
        PCWSTR      pszName,
        PCWSTR      pszValue
    )
    {
        HRESULT hr = S_OK;
        if (FAILED(hr = _strName.Copy(pszName)) ||
            FAILED(hr = _strValue.Copy(pszValue))) 
        {
        }
        return hr;        
    }

    VOID 
    Reference() const
    {
        InterlockedIncrement(&_cRefs);
    }

    VOID
    Dereference() const
    {
        if (InterlockedDecrement(&_cRefs) == 0)
        {
            delete this;
        }
    }

    PWSTR  const
    QueryName()
    {
        return _strName.QueryStr();
    }

    PWSTR const
    QueryValue()
    {
        return _strValue.QueryStr();
    }

private:
    ~ENVIRONMENT_VAR_ENTRY()
    {
    }

    STRU                _strName;
    STRU                _strValue;
    mutable LONG        _cRefs;
};

class ENVIRONMENT_VAR_HASH : public HASH_TABLE<ENVIRONMENT_VAR_ENTRY, PWSTR>
{
public:
    ENVIRONMENT_VAR_HASH()
    {
    }

    PWSTR
    ExtractKey(
        ENVIRONMENT_VAR_ENTRY *   pEntry
    )
    {
        return pEntry->QueryName();
    }

    DWORD
    CalcKeyHash(
        PWSTR   pszName
    )
    {
        return HashStringNoCase(pszName);
    }

    BOOL
    EqualKeys(
        PWSTR   pszName1,
        PWSTR   pszName2
    )
    {
        return (_wcsicmp(pszName1, pszName2) == 0);
    }

    VOID
    ReferenceRecord(
        ENVIRONMENT_VAR_ENTRY *   pEntry
    )
    {
        pEntry->Reference();
    }

    VOID
    DereferenceRecord(
        ENVIRONMENT_VAR_ENTRY *   pEntry
    )
    {
        pEntry->Dereference();
    }

    static
    VOID
    CopyToMultiSz(
        ENVIRONMENT_VAR_ENTRY *   pEntry,
        PVOID                     pvData
    )
    {
        STRU     strTemp;
        MULTISZ   *pMultiSz = static_cast<MULTISZ *>(pvData);
        DBG_ASSERT(pMultiSz);
        DBG_ASSERT(pEntry);
        strTemp.Copy(pEntry->QueryName());
        strTemp.Append(pEntry->QueryValue());
        pMultiSz->Append(strTemp.QueryStr());
    }

    static
    VOID
    CopyToTable(
        ENVIRONMENT_VAR_ENTRY *   pEntry,
        PVOID                     pvData
    )
    {
        // best effort copy, ignore the failure
        ENVIRONMENT_VAR_ENTRY *   pNewEntry = new ENVIRONMENT_VAR_ENTRY();
        if (pNewEntry != NULL)
        {
            pNewEntry->Initialize(pEntry->QueryName(), pEntry->QueryValue());
            ENVIRONMENT_VAR_HASH *pHash = static_cast<ENVIRONMENT_VAR_HASH *>(pvData);
            DBG_ASSERT(pHash);
            pHash->InsertRecord(pNewEntry);
            // Need to dereference as InsertRecord references it now
            pNewEntry->Dereference();
        }
    }

    static
    VOID
    AppendEnvironmentVariables
    (
        ENVIRONMENT_VAR_ENTRY *   pEntry,
        PVOID                     pvData
    )
    {
        UNREFERENCED_PARAMETER(pvData);

        HRESULT hr = S_OK;
        DWORD   dwResult = 0;
        DWORD   dwError = 0;
        STRU    struNameBuffer;
        STACK_STRU(struValueBuffer, 300);
        BOOL    fFound = FALSE;

        // pEntry->QueryName includes the trailing =, remove it before calling stru
        if (FAILED(hr = struNameBuffer.Copy(pEntry->QueryName())))
        {
            goto Finished;
        }
        dwResult = struNameBuffer.LastIndexOf(L'=');
        if (dwResult != -1)
        {
            struNameBuffer.QueryStr()[dwResult] = L'\0';
            if (FAILED(hr = struNameBuffer.SyncWithBuffer()))
            {
                goto Finished;
            }
        }

        dwResult = GetEnvironmentVariable(struNameBuffer.QueryStr(), struValueBuffer.QueryStr(), struValueBuffer.QuerySizeCCH());
        if (dwResult == 0)
        {
            dwError = GetLastError();
            // Windows API (e.g., CreateProcess) allows variable with empty string value
            // in such case dwResult will be 0 and dwError will also be 0
            // As UI and CMD does not allow empty value, ignore this environment var
            if (dwError != ERROR_ENVVAR_NOT_FOUND && dwError != ERROR_SUCCESS)
            {
                hr = HRESULT_FROM_WIN32(dwError);
                goto Finished;
            }
        }
        else if (dwResult > struValueBuffer.QuerySizeCCH())
        {
            // have to increase the buffer and try get environment var again
            struValueBuffer.Reset();
            struValueBuffer.Resize(dwResult + (DWORD)wcslen(pEntry->QueryValue()) + 2); // for null char and semicolon
            dwResult = GetEnvironmentVariable(struNameBuffer.QueryStr(),
                struValueBuffer.QueryStr(),
                struValueBuffer.QuerySizeCCH());
            if (struValueBuffer.IsEmpty())
            {
                hr = E_UNEXPECTED;
                goto Finished;
            }
            fFound = TRUE;
        }
        else
        {
            fFound = TRUE;
        }

        if (FAILED(hr = struValueBuffer.SyncWithBuffer()))
        {
            goto Finished;
        }

        if (fFound)
        {
            if (FAILED(hr = struValueBuffer.Append(L";")))
            {
                goto Finished;
            }
        }
        if (FAILED(hr = struValueBuffer.Append(pEntry->QueryValue())))
        {
            goto Finished;
        }

        if (FAILED(hr = pEntry->Initialize(pEntry->QueryName(), struValueBuffer.QueryStr())))
        {
            goto Finished;
        }

    Finished:
        // TODO besides calling SetLastError, is there anyway to propagate failures to set the environment variable?
        return;
    }

    static
    VOID
    SetEnvironmentVariables
    (
        ENVIRONMENT_VAR_ENTRY *   pEntry,
        PVOID                     pvData
    )
    {
        UNREFERENCED_PARAMETER(pvData);
        HRESULT hr = S_OK;
        DWORD dwResult = 0;
        STRU struNameBuffer;

        // pEntry->QueryName includes the trailing =, remove it before calling SetEnvironmentVariable.
        if (FAILED(hr = struNameBuffer.Copy(pEntry->QueryName())))
        {
            goto Finished;
        }
        dwResult = struNameBuffer.LastIndexOf(L'=');
        if (dwResult != -1)
        {
            struNameBuffer.QueryStr()[dwResult] = L'\0';
            if (FAILED(hr = struNameBuffer.SyncWithBuffer()))
            {
                goto Finished;
            }
        }

        dwResult = SetEnvironmentVariable(struNameBuffer.QueryStr(), pEntry->QueryValue());
        if (dwResult == 0)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }

    Finished:
        return;
    }

    static
    HRESULT
    InitEnvironmentVariablesTable
    (
        _In_ ENVIRONMENT_VAR_HASH*      pInEnvironmentVarTable,
        BOOL                            fWindowsAuthEnabled,
        BOOL                            fBasicAuthEnabled,
        BOOL                            fAnonymousAuthEnabled,
        ENVIRONMENT_VAR_HASH**          ppEnvironmentVarTable
    )
    {
        HRESULT hr = S_OK;
        BOOL    fFound = FALSE;
        DWORD   dwResult, dwError;
        STRU    strIisAuthEnvValue;
        STACK_STRU(strStartupAssemblyEnv, 1024);
        ENVIRONMENT_VAR_ENTRY* pHostingEntry = NULL;
        ENVIRONMENT_VAR_ENTRY* pIISAuthEntry = NULL;
        ENVIRONMENT_VAR_HASH* pEnvironmentVarTable = NULL;

        pEnvironmentVarTable = new ENVIRONMENT_VAR_HASH();
        if (pEnvironmentVarTable == NULL)
        {
            hr = E_OUTOFMEMORY;
            goto Finished;
        }

        //
        // few environment variables expected, small bucket size for hash table
        //
        if (FAILED(hr = pEnvironmentVarTable->Initialize(37 /*prime*/)))
        {
            goto Finished;
        }

        // copy the envirable hash table (from configuration) to a temp one as we may need to remove elements 
        pInEnvironmentVarTable->Apply(ENVIRONMENT_VAR_HASH::CopyToTable, pEnvironmentVarTable);
        if (pEnvironmentVarTable->Count() != pInEnvironmentVarTable->Count())
        {
            // hash table copy failed
            hr = E_UNEXPECTED;
            goto Finished;
        }

        pEnvironmentVarTable->FindKey((PWSTR)ASPNETCORE_IIS_AUTH_ENV_STR, &pIISAuthEntry);
        if (pIISAuthEntry != NULL)
        {
            // user defined ASPNETCORE_IIS_HTTPAUTH in configuration, wipe it off
            pIISAuthEntry->Dereference();
            pEnvironmentVarTable->DeleteKey((PWSTR)ASPNETCORE_IIS_AUTH_ENV_STR);
        }

        if (fWindowsAuthEnabled)
        {
            strIisAuthEnvValue.Copy(ASPNETCORE_IIS_AUTH_WINDOWS);
        }

        if (fBasicAuthEnabled)
        {
            strIisAuthEnvValue.Append(ASPNETCORE_IIS_AUTH_BASIC);
        }

        if (fAnonymousAuthEnabled)
        {
            strIisAuthEnvValue.Append(ASPNETCORE_IIS_AUTH_ANONYMOUS);
        }

        if (strIisAuthEnvValue.IsEmpty())
        {
            strIisAuthEnvValue.Copy(ASPNETCORE_IIS_AUTH_NONE);
        }

        pIISAuthEntry = new ENVIRONMENT_VAR_ENTRY();
        if (pIISAuthEntry == NULL)
        {
            hr = E_OUTOFMEMORY;
            goto Finished;
        }
        if (FAILED(hr = pIISAuthEntry->Initialize(ASPNETCORE_IIS_AUTH_ENV_STR, strIisAuthEnvValue.QueryStr())) ||
            FAILED(hr = pEnvironmentVarTable->InsertRecord(pIISAuthEntry)))
        {
            goto Finished;
        }

        // Compiler is complaining about conversion between PCWSTR and PWSTR here.
        // Explictly casting. 
        pEnvironmentVarTable->FindKey((PWSTR)HOSTING_STARTUP_ASSEMBLIES_NAME, &pHostingEntry);
        if (pHostingEntry != NULL)
        {
            // user defined ASPNETCORE_HOSTINGSTARTUPASSEMBLIES in configuration
            // the value will be used in OutputEnvironmentVariables. Do nothing here
            pHostingEntry->Dereference();
            pHostingEntry = NULL;
            goto Skipped;
        }

        //check whether ASPNETCORE_HOSTINGSTARTUPASSEMBLIES is defined in system
        dwResult = GetEnvironmentVariable(HOSTING_STARTUP_ASSEMBLIES_ENV_STR,
            strStartupAssemblyEnv.QueryStr(),
            strStartupAssemblyEnv.QuerySizeCCH());
        if (dwResult == 0)
        {
            dwError = GetLastError();
            // Windows API (e.g., CreateProcess) allows variable with empty string value
            // in such case dwResult will be 0 and dwError will also be 0
            // As UI and CMD does not allow empty value, ignore this environment var
            if (dwError != ERROR_ENVVAR_NOT_FOUND && dwError != ERROR_SUCCESS)
            {
                hr = HRESULT_FROM_WIN32(dwError);
                goto Finished;
            }
        }
        else if (dwResult > strStartupAssemblyEnv.QuerySizeCCH())
        {
            // have to increase the buffer and try get environment var again
            strStartupAssemblyEnv.Reset();
            strStartupAssemblyEnv.Resize(dwResult + (DWORD)wcslen(HOSTING_STARTUP_ASSEMBLIES_VALUE) + 1);
            dwResult = GetEnvironmentVariable(HOSTING_STARTUP_ASSEMBLIES_ENV_STR,
                strStartupAssemblyEnv.QueryStr(),
                strStartupAssemblyEnv.QuerySizeCCH());
            if (strStartupAssemblyEnv.IsEmpty())
            {
                hr = E_UNEXPECTED;
                goto Finished;
            }
            fFound = TRUE;
        }
        else
        {
            fFound = TRUE;
        }

        strStartupAssemblyEnv.SyncWithBuffer();
        if (fFound)
        {
            strStartupAssemblyEnv.Append(L";");
        }
        strStartupAssemblyEnv.Append(HOSTING_STARTUP_ASSEMBLIES_VALUE);

        // the environment variable was not defined, create it and add to hashtable
        pHostingEntry = new ENVIRONMENT_VAR_ENTRY();
        if (pHostingEntry == NULL)
        {
            hr = E_OUTOFMEMORY;
            goto Finished;
        }
        if (FAILED(hr = pHostingEntry->Initialize(HOSTING_STARTUP_ASSEMBLIES_NAME, strStartupAssemblyEnv.QueryStr())) ||
            FAILED(hr = pEnvironmentVarTable->InsertRecord(pHostingEntry)))
        {
            goto Finished;
        }

    Skipped:
        *ppEnvironmentVarTable = pEnvironmentVarTable;
        pEnvironmentVarTable = NULL;

    Finished:
        if (pHostingEntry != NULL)
        {
            pHostingEntry->Dereference();
            pHostingEntry = NULL;
        }

        if (pIISAuthEntry != NULL)
        {
            pIISAuthEntry->Dereference();
            pIISAuthEntry = NULL;
        }

        if (pEnvironmentVarTable != NULL)
        {
            pEnvironmentVarTable->Clear();
            delete pEnvironmentVarTable;
            pEnvironmentVarTable = NULL;
        }
        return hr;
    }

private:
    ENVIRONMENT_VAR_HASH(const ENVIRONMENT_VAR_HASH &);
    void operator=(const ENVIRONMENT_VAR_HASH &);
};
