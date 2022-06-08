
#include <Windows.h>

//来源：Win7 SP1 64位 旗舰版
//参考：https://www.remkoweijnen.nl/blog/2010/06/15/having-fun-with-windows-licensing/
//无忧论坛有一些补充信息，但是软件要权限才能下载，一切都只能靠自己
//https://bbs.pcbeta.com/viewthread-1774832-1-1.html
//新资料：
//https://www.geoffchappell.com/studies/windows/km/ntoskrnl/api/ex/slmem/productpolicy.htm

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
	BYTE bh=(b&0xF0)>>4;
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
				//字符串长度非0时，总长度计入末尾的0，字符串长度为0时，总长度却为0
				//可以分配出长度为0的内存，但wcscpy写入末尾的0时会出问题，必须分开处理
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

struct EntryExpand
{
	ProductPolicyEntry Head;
	WCHAR* Name;
	BYTE* Value;
};

ProductPolicyHeader ExpandHead;
EntryExpand* ExpandArray;
int ExpandNumber;
DWORD ExpandTail;
BOOL IsExpand=FALSE;

//返回所需缓冲区大小
int PolicyCreateEmptyBlock(BYTE* Buffer)
{
	if (Buffer==NULL)
		return sizeof(ProductPolicyHeader)+4;

	ProductPolicyHeader* Head=(ProductPolicyHeader*)Buffer;
	Head->TotalSize=sizeof(ProductPolicyHeader)+4;
	Head->DataSize=0;
	Head->CheckSize=4;
	Head->BootFlag=0;
	Head->HeadCheck=1;
	DWORD* Tail=(DWORD*)(Buffer+sizeof(ProductPolicyHeader));
	*Tail=4+0x41;
	return Head->TotalSize;
}

//返回项数
int PolicyExpandData(BYTE* InputData)
{
	//没有检测数据有效性
	ExpandHead=*(ProductPolicyHeader*)InputData;
	ExpandTail=*(DWORD*)(InputData+sizeof(ProductPolicyHeader)+ExpandHead.DataSize);

	ExpandNumber=0;
	int RemainSize=ExpandHead.DataSize;

	ProductPolicyEntry* EntryPtr=(ProductPolicyEntry*)(InputData+sizeof(ProductPolicyHeader));
	while (RemainSize>0)
	{
		RemainSize=RemainSize-EntryPtr->BlockSize;
		EntryPtr=(ProductPolicyEntry*)((BYTE*)EntryPtr+EntryPtr->BlockSize);
		ExpandNumber++;
	}

	ExpandArray=new EntryExpand[ExpandNumber];
	EntryPtr=(ProductPolicyEntry*)(InputData+sizeof(ProductPolicyHeader));
	for (int i=0;i<ExpandNumber;i++)
	{
		ExpandArray[i].Head=*EntryPtr;

		WCHAR* NamePtr=(WCHAR*)((BYTE*)EntryPtr+sizeof(ProductPolicyEntry));
		ExpandArray[i].Name=new WCHAR[EntryPtr->NameSize/sizeof(WCHAR)+1];
		memcpy(ExpandArray[i].Name,NamePtr,EntryPtr->NameSize);
		ExpandArray[i].Name[EntryPtr->NameSize]=0;

		BYTE* ValuePtr=(BYTE*)EntryPtr+sizeof(ProductPolicyEntry)+EntryPtr->NameSize;
		ExpandArray[i].Value=new BYTE[EntryPtr->ValueSize];
		memcpy(ExpandArray[i].Value,ValuePtr,EntryPtr->ValueSize);

		EntryPtr=(ProductPolicyEntry*)((BYTE*)EntryPtr+EntryPtr->BlockSize);
	}

	IsExpand=TRUE;
	return ExpandNumber;
}

void SelectSort(EntryExpand* ExpandArray,int ExpandNumber)
{
	int SortedNum=0;
	int MinItem;
	for (int i=0;i<ExpandNumber;i++)
	{
		MinItem=SortedNum;	//UnsortedIndex=SortedNum-1+1,
		for (int j=SortedNum;j<ExpandNumber-SortedNum;j++)
		{
			//从小到大排序
			if (wcscmp(ExpandArray[j].Name,ExpandArray[MinItem].Name)<0)
			{
				//相比插入排序，必须扫描全部，不能break
				MinItem=j;
			}
		}
		//相比插入排序，无需向后移动元素，只要交换目标元素
		EntryExpand Temp=ExpandArray[SortedNum];
		ExpandArray[SortedNum]=ExpandArray[MinItem];
		ExpandArray[MinItem]=Temp;
		SortedNum++;
	}
}

//返回所需缓冲区大小
int PolicyRepackData(BYTE* Buffer)
{
	if (Buffer==NULL)
		return ExpandHead.TotalSize;

	SelectSort(ExpandArray,ExpandNumber);
	memset(Buffer,0,ExpandHead.TotalSize);
	*(ProductPolicyHeader*)Buffer=ExpandHead;
	*(DWORD*)(Buffer+sizeof(ProductPolicyHeader)+ExpandHead.DataSize)=ExpandTail;

	ProductPolicyEntry* EntryPtr=(ProductPolicyEntry*)(Buffer+sizeof(ProductPolicyHeader));
	for (int i=0;i<ExpandNumber;i++)
	{
		*EntryPtr=ExpandArray[i].Head;

		WCHAR* NamePtr=(WCHAR*)((BYTE*)EntryPtr+sizeof(ProductPolicyEntry));
		memcpy(NamePtr,ExpandArray[i].Name,ExpandArray[i].Head.NameSize);
		delete[] ExpandArray[i].Name;

		BYTE* ValuePtr=(BYTE*)EntryPtr+sizeof(ProductPolicyEntry)+EntryPtr->NameSize;
		memcpy(ValuePtr,ExpandArray[i].Value,ExpandArray[i].Head.ValueSize);
		delete[] ExpandArray[i].Value;

		//填充字节直接空出就可以了

		EntryPtr=(ProductPolicyEntry*)((BYTE*)EntryPtr+EntryPtr->BlockSize);
	}
	delete[] ExpandArray;

	IsExpand=FALSE;
	return ExpandHead.TotalSize;
}

//返回是否成功，重名会失败
//注意如果类型为SL_DATA_SZ，长度需要计入末尾的0
BOOL PolicyInsertEntry(WCHAR* Name,SLDATATYPE ValueType,int ValueSize,void* Value)
{
	if (!IsExpand)
		return FALSE;

	for (int i=0;i<ExpandNumber;i++)
	{
		if (wcscmp(ExpandArray[i].Name,Name)==0)
			return FALSE;
	}

	//重新分配内存复制过去，效率不高但是比较简单；内层指针不受影响
	EntryExpand* NewExpandArray=new EntryExpand[ExpandNumber+1];
	memcpy(NewExpandArray,ExpandArray,sizeof(EntryExpand)*ExpandNumber);
	delete[] ExpandArray;
	ExpandArray=NewExpandArray;
	
	//没检查参数正确性，比如Name长度不能为0
	ExpandArray[ExpandNumber].Head.Zero=0;
	ExpandArray[ExpandNumber].Head.NotifyFlag=0;
	ExpandArray[ExpandNumber].Head.NameSize=wcslen(Name)*sizeof(WCHAR);
	ExpandArray[ExpandNumber].Head.ValueType=ValueType;
	ExpandArray[ExpandNumber].Head.ValueSize=ValueSize;
	ExpandArray[ExpandNumber].Head.BlockSize=sizeof(ProductPolicyEntry)+ExpandArray[ExpandNumber].Head.NameSize+
		ExpandArray[ExpandNumber].Head.ValueSize;
	//每一项都对齐到4字节，则总的数据也一定对齐到4字节
	int PadSize=4-ExpandArray[ExpandNumber].Head.BlockSize%4;
	ExpandArray[ExpandNumber].Head.BlockSize+=PadSize;

	ExpandArray[ExpandNumber].Name=new WCHAR[wcslen(Name)+1];
	wcscpy(ExpandArray[ExpandNumber].Name,Name);

	ExpandArray[ExpandNumber].Value=new BYTE[ValueSize];
	memcpy(ExpandArray[ExpandNumber].Value,Value,ValueSize);

	ExpandNumber++;
	return TRUE;
}

//返回是否成功，不存在会失败
BOOL PolicyDeleteEntry(WCHAR* Name)
{
	if (!IsExpand)
		return FALSE;

	for (int i=0;i<ExpandNumber;i++)
	{
		if (wcscmp(ExpandArray[i].Name,Name)==0)
		{
			delete[] ExpandArray[i].Name;
			delete[] ExpandArray[i].Value;
			//打包时再排序，这里不用管顺序
			ExpandArray[i]=ExpandArray[ExpandNumber-1];
			ExpandNumber--;
			return TRUE;
		}
	}
	return FALSE;
}

//返回是否成功，不存在会失败，重名但类型不同也会失败
BOOL PolicyModifyEntry(WCHAR* Name,SLDATATYPE ValueType,int ValueSize,void* Value)
{
	if (!IsExpand)
		return FALSE;

	for (int i=0;i<ExpandNumber;i++)
	{
		if (wcscmp(ExpandArray[i].Name,Name)==0)
		{
			if (ExpandArray[i].Head.ValueType!=ValueType)
				return FALSE;

			delete[] ExpandArray[i].Value;	
			ExpandArray[i].Head.ValueSize=ValueSize;
			ExpandArray[i].Head.BlockSize=sizeof(ProductPolicyEntry)+ExpandArray[i].Head.NameSize+
				ExpandArray[i].Head.ValueSize;
			int PadSize=4-ExpandArray[i].Head.BlockSize%4;
			ExpandArray[i].Head.BlockSize+=PadSize;
			return TRUE;
		}
	}
	return FALSE;
}

