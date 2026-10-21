/* Checks patch 0021: a key saved with RegSaveKey is padded past a binary hive header
 * (ACE-Base64.dll reads 0x1000 bytes into the file looking for "hbin"), and
 * RegRestoreKey still loads the padded file. Prints "regsave size=<n>" and "regsave restore=<n>".
 */
#include <windows.h>
#include <stdio.h>

static void enable(const wchar_t *name)
{
    HANDLE token;
    TOKEN_PRIVILEGES tp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid))
        AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(token);
}

int main(void)
{
    HKEY key = NULL, restored = NULL;
    WCHAR dir[MAX_PATH], file[MAX_PATH];
    DWORD size = 0, value = 0, len = sizeof(value), type;
    HANDLE h;
    LONG err;

    enable(L"SeBackupPrivilege");
    enable(L"SeRestorePrivilege");
    err = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\GryphfieldRegsaveProbe", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL);
    if (err) { printf("create failed %ld\n", err); return 2; }
    value = 1234;
    RegSetValueExW(key, L"probe", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    GetTempPathW(MAX_PATH, dir);
    GetTempFileNameW(dir, L"rsp", 0, file);
    DeleteFileW(file);
    err = RegSaveKeyW(key, file, NULL);
    if (err) { printf("RegSaveKey failed %ld\n", err); return 2; }
    h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) { size = GetFileSize(h, NULL); CloseHandle(h); }
    printf("regsave size=%lu\n", (unsigned long)size);

    err = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\GryphfieldRegsaveRestored", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &restored, NULL);
    if (!err) err = RegRestoreKeyW(restored, file, REG_FORCE_RESTORE);
    value = 0;
    if (!err) err = RegQueryValueExW(restored, L"probe", NULL, &type, (BYTE *)&value, &len);
    printf("regsave restore=%lu err=%ld\n", (unsigned long)value, (long)err);
    DeleteFileW(file);
    RegCloseKey(restored);
    RegCloseKey(key);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\GryphfieldRegsaveRestored");
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\GryphfieldRegsaveProbe");
    fflush(stdout);
    return size > 0x1020 && value == 1234 ? 0 : 1;
}
