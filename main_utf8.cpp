
#include <Windows.h>

//来源：Win7 SP1 64位 旗舰版
//参考：https://www.remkoweijnen.nl/blog/2010/06/15/having-fun-with-windows-licensing/
//无忧论坛有一些补充信息，但是软件要权限才能下载，一切都只能靠自己
//https://bbs.pcbeta.com/viewthread-1774832-1-1.html

//内核负责维护License数据，一份放在注册表里，另一份放在内存的ExpLicensingView里
//还有一份展开的数据，放在内存的ExpLicensingDescriptors里
//系统通过ExUpdateLicenseData()将自带的ExpBootLicensingData设为初始的内存数据
//后续用户通过ExUpdateLicenseData()动态更新License数据，除了更新内存，还会写入注册表
//动态更新License数据时，BootFlag必须为0，否则调用失败

//项数据按Name的字符串大小排序，加速查找过程，规则是：
//1.字符串对齐开头进行比较，第一个遇到的不同字符决定大小
//2.若较短字符串耗尽，且字符全都相等，长字符串更大
//当新数据中某项在旧数据中没有，或新数据中某项在旧数据中存在但值变更了
//如果这项的NotifyFlag为2，ExUpdateLicenseData()会报告这种情况，为0时不报告
//可能检测到改变会通知所有组件，相应组件发现自己的配置信息改变，重新设定数据

#pragma pack(push, 2)
//整个数据块由Head，Data和Check三部分组成，顺序排列无空隙
//Check用于尾部验证，长度固定为4，值为其长度+0x41
//整个数据块大小要对齐到4字节，且不能超过0x10000（64KB）
struct ProductPolicyHeader
{
	DWORD TotalSize;
	DWORD DataSize;
	DWORD CheckSize;	//末尾的校验数据长度，必须为4
	DWORD BootFlag;		//系统自带的数据为1，动态设置的数据为0
	DWORD HeadCheck;	//头部验证，必须为1
};

typedef enum _tagSLDATATYPE
{
	SL_DATA_NONE        = REG_NONE,
	SL_DATA_SZ          = REG_SZ,
	SL_DATA_DWORD       = REG_DWORD,
	SL_DATA_BINARY      = REG_BINARY,
	SL_DATA_MULTI_SZ    = REG_MULTI_SZ,
	SL_DATA_SUM         = 100,
} SLDATATYPE;

//每一项由Head，Name，Value组成，顺序排列无空隙，但末尾会有2字节或4字节填充
//填充必须存在，内容为0，填充字节的个数要保证当前项对齐到4字节
struct ProductPolicyEntry
{
	WORD BlockSize;
	WORD NameSize;
	WORD ValueType;	//SLDATATYPE
	WORD ValueSize;
	DWORD NotifyFlag;	//值为2时，新增或修改此项会触发报告，为0时不报告
	DWORD Zero;		//总为0
};
#pragma pack(pop)

int HexByteToText(BYTE b,WCHAR* Text)
{
	BYTE bh=(b&0xF0)>>8;
	BYTE bl=b&0x0F;
	if (bh<10)
		Text[0]=bh+'0';
	else
		Text[0]=bh-10+'A';
	if (bl<10)
		Text[1]=bl+'0';
	else
		Text[1]=bl-10+'A';
	Text[2]=0;
	return 2;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	HKEY hKey;
	RegCreateKey(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\ProductOptions",&hKey);
	DWORD RegValueType=REG_NONE;
	DWORD RegValueSize=0;
	RegQueryValueEx(hKey,L"ProductPolicy",NULL,&RegValueType,NULL,&RegValueSize);
	BYTE* RegValue=new BYTE[RegValueSize];
	RegQueryValueEx(hKey,L"ProductPolicy",NULL,&RegValueType,RegValue,&RegValueSize);
	RegCloseKey(hKey);

	WCHAR* OutBuffer=new WCHAR[0x20000];
	int OutCur=0;

	ProductPolicyHeader* Header=(ProductPolicyHeader*)RegValue;
	ProductPolicyEntry* EntryPtr=(ProductPolicyEntry*)(RegValue+sizeof(ProductPolicyHeader));
	int RemainSize=Header->DataSize;
	int Index=0;

	while (RemainSize>0)
	{
		WCHAR* NamePtr=(WCHAR*)((BYTE*)EntryPtr+sizeof(ProductPolicyEntry));
		BYTE* ValuePtr=(BYTE*)EntryPtr+sizeof(ProductPolicyEntry)+EntryPtr->NameSize;

		WCHAR* NameBuffer=new WCHAR[EntryPtr->NameSize/sizeof(WCHAR)+1];
		memcpy(NameBuffer,NamePtr,EntryPtr->NameSize);
		NameBuffer[EntryPtr->NameSize/sizeof(WCHAR)]=0;

		WCHAR* ValueBuffer;
		switch (EntryPtr->ValueType)
		{
		case SL_DATA_SZ:
		case SL_DATA_MULTI_SZ:
			{
				if (EntryPtr->ValueSize!=0)
				{
					ValueBuffer=new WCHAR[EntryPtr->ValueSize];
					wcscpy(ValueBuffer,(WCHAR*)ValuePtr);
				}
				else
				{
					ValueBuffer=new WCHAR[1];
					ValueBuffer[0]=0;
				}
			}
			break;
		case SL_DATA_DWORD:
			{
				ValueBuffer=new WCHAR[11];
				wsprintf(ValueBuffer,L"0x%08X",*(DWORD*)ValuePtr);
			}
			break;
		case SL_DATA_BINARY:
			{
				ValueBuffer=new WCHAR[EntryPtr->ValueSize*3+1];
				int cur=0;
				for (int i=0;i<EntryPtr->ValueSize;i++)
				{
					HexByteToText(ValuePtr[i],ValueBuffer+cur);
					*(ValueBuffer+cur+2)=' ';
					cur+=3;
				}
				ValueBuffer[cur]=0;
			}
			break;
		default:
			{
				ValueBuffer=new WCHAR[20];
				wsprintf(ValueBuffer,L"[type=%d]",EntryPtr->ValueType);
			}
			break;
		}

		int NewLen=wsprintf(OutBuffer+OutCur,L"%d: %s=%s NotifyFlag=%d\r\n",Index,NameBuffer,ValueBuffer,EntryPtr->NotifyFlag);
		OutCur=OutCur+NewLen;

		delete[] NameBuffer;
		delete[] ValueBuffer; 

		RemainSize=RemainSize-EntryPtr->BlockSize;
		EntryPtr=(ProductPolicyEntry*)((BYTE*)EntryPtr+EntryPtr->BlockSize);
		Index++;
	}

	HANDLE hFile=CreateFile(L"ProductPolicy.txt",GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	DWORD BOM=2;
	WriteFile(hFile,"\xFF\xFE",BOM,(DWORD*)&BOM,NULL);
	WriteFile(hFile,OutBuffer,OutCur*sizeof(WCHAR),(DWORD*)&OutCur,NULL);
	CloseHandle(hFile);

	delete[] OutBuffer;
	delete[] RegValue;
	return 0;
};