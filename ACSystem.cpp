#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <queue>
#include <thread>
#include <psapi.h>
#include "acsdef.h"
SIZE_T pWorkingSetSize = 0;
bool monitoring = false;
void monitorMemory(HANDLE handle) 
{
	DWORD exitCode;
	GetExitCodeProcess(handle, &exitCode);
	PROCESS_MEMORY_COUNTERS p;
	while(exitCode == STILL_ACTIVE)
	{
		monitoring = true;
		GetExitCodeProcess(handle, &exitCode);
		GetProcessMemoryInfo(handle,&p,sizeof(p));
		pWorkingSetSize = std::max(p.WorkingSetSize,pWorkingSetSize);
	}
	monitoring = false;
}
void outputMemory()
{
	while(monitoring)
	{
		Sleep(1000);
		std::cout<<"Child process memory used(Bytes) :　"<<pWorkingSetSize<<std::endl;
	}
	std::cout<<"Child process died !"<<std::endl;
}
long id = 0;
long idp()
{
	return id++;
}
std::string removeTrailingNewlines(std::string str) 
{
	// 循环删除末尾的 \n 或 \r
	while (!str.empty() && (str.back() == '\n' || str.back() == '\r')) 
	{
		str.pop_back();
	}
	return str;
}
struct TestingRequest
{
	std::string code = "";
	int questionCount = 0;
	int id = 0;
};
struct TestingResult
{
	int result;
};
TestingRequest getRequest(std::string code,int questionCount)
{
	TestingRequest request;
	request.code = code;
	request.questionCount = questionCount;
	request.id = idp();
	return request;
}
int execCommand(const std::string& cmd, const std::string& input = "" , const std::string& answer = "",int timelimit = 0,SIZE_T memlimit = 0) 
{
	std::cout<<"Time limit : "<<timelimit<<std::endl;
	std::cout<<"Memory limit : "<<memlimit<<std::endl;
	// 管道1：子进程输出 → 父进程读取（hChildOutWrite 子进程写，hParentInRead 父进程读）
	HANDLE hParentInRead, hChildOutWrite;
	// 管道2：父进程输入 → 子进程读取（hParentOutWrite 父进程写，hChildInRead 子进程读）
	HANDLE hParentOutWrite, hChildInRead;

	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE; // 允许子进程继承句柄
	
	// 创建输出管道（子进程→父进程）
	// 创建输入管道（父进程→子进程）
	if (!CreatePipe(&hParentInRead, &hChildOutWrite, &sa, 0) || !CreatePipe(&hChildInRead, &hParentOutWrite, &sa, 0)) 
	{
		CloseHandle(hParentInRead);
		CloseHandle(hChildInRead);
		CloseHandle(hParentInRead);
		CloseHandle(hChildOutWrite);
		return ACSRUNTIME_ERROR;
	}

	// 设置：父进程的读/写端不继承（避免子进程干扰）
	SetHandleInformation(hParentInRead, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(hParentOutWrite, HANDLE_FLAG_INHERIT, 0);
	DWORD nowait = timelimit;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	memset(&si,0,sizeof(si));
	memset(&pi,0,sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;       // 隐藏子进程窗口
	si.hStdOutput = hChildOutWrite; // 子进程stdout → 输出管道写端
	si.hStdError = hChildOutWrite;  // 子进程stderr → 输出管道写端（合并输出）
	si.hStdInput = hChildInRead;    // 子进程stdin → 输入管道读端
	
	// 关键：CreateProcess要求命令行是可修改的字符串，复制到非const缓冲区
	std::vector<char> cmdBuffer(cmd.begin(), cmd.end());
	cmdBuffer.push_back('\0'); // 手动添加字符串结束符
	
	// 创建子进程
	if (!CreateProcessA(
						NULL,                       // 不指定可执行文件路径（由命令行决定）
						cmdBuffer.data(),           // 可修改的命令行缓冲区（ANSI版本）
						NULL,                       // 进程安全描述符
						NULL,                       // 线程安全描述符
						TRUE,                       // 允许句柄继承（子进程需要继承管道写端/读端）
						CREATE_NO_WINDOW,           // 不创建子进程窗口
						NULL,                       // 继承父进程环境变量
						NULL,                       // 继承父进程工作目录（确保untitled5.exe在当前目录）
						&si,                        // 启动信息（重定向输入输出）
						&pi                         // 进程信息输出
						)) {
		// 失败时关闭所有已创建的句柄
		CloseHandle(hParentInRead);
		CloseHandle(hChildOutWrite);
		CloseHandle(hParentOutWrite);
		CloseHandle(hChildInRead);
		return ACSRUNTIME_ERROR;
	}
	
	// 父进程不需要的子进程端句柄，立即关闭（否则会阻塞）
	CloseHandle(hChildOutWrite);
	CloseHandle(hChildInRead);
	
	// 给子进程发送输入（input = "hi"）
	DWORD bytesWritten = 0;
	if (!input.empty()) {
		if (!WriteFile(
					   hParentOutWrite,         // 父进程的输入管道写端
					   input.c_str(),           // 要发送的内容
					   (DWORD)input.size(),     // 内容长度
					   &bytesWritten,           // 实际写入字节数
					   NULL
					   )) {
			return ACSRUNTIME_ERROR;
		}
		std::cout << "Sent to child process: \n" << input << std::endl;
	}
	// 发送完输入后关闭写端（告诉子进程没有更多输入了，避免子进程阻塞在read）
	CloseHandle(hParentOutWrite);
	
	// 读取子进程的输出
	
	std::thread memoryMonitor (monitorMemory,pi.hProcess);
	memoryMonitor.detach();
	std::thread memoryOutput (outputMemory);
	memoryOutput.detach();
	// 等待子进程结束，释放资源
	WaitForSingleObject(pi.hProcess, timelimit);
	if(pWorkingSetSize>memlimit)
	{
		if(TerminateProcess(pi.hProcess,10000)!=0)
		{
			std::cout<<"TerminateProcess() to Child process (MLE)!"<<std::endl;
			return MEMORY_LIMIT_ERROR;
		}
		
	}
	DWORD exitCode;
	if (!GetExitCodeProcess(pi.hProcess, &exitCode)) 
	{
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return ACSRUNTIME_ERROR;
	} 
	if(exitCode == STILL_ACTIVE)
	{
		std::cout<<"Child process can't exit!"<<std::endl;
		if(TerminateProcess(pi.hProcess,10000)!=0)
		{
			std::cout<<"TerminateProcess() to Child process (TLE)!"<<std::endl;
			return TIME_LIMIT_ERROR;
		}
		return ACSRUNTIME_ERROR;
	}
	std::cout<<"Child process exit code:"<<exitCode<<std::endl;
	std::string output;
	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile(
					hParentInRead,              // 父进程的输出管道读端
					buffer,                     // 接收缓冲区
					sizeof(buffer) - 1,         // 预留1字节存字符串结束符
					&bytesRead,                 // 实际读取字节数
					NULL
					) && bytesRead > 0) {
		buffer[bytesRead] = '\0';   // 手动添加结束符
		output += buffer;
	}
	
	// 关闭父进程的读端
	CloseHandle(hParentInRead);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	if(exitCode!=0)
	{
		return RUNTIME_ERROR;
	}
	std::cout << "Received: \n" << removeTrailingNewlines(output) << std::endl;
	if(answer==removeTrailingNewlines(output))
	{
		return ANSWER_CORRECT;
	}
	return WRONG_ANSWER;
}
std::string getTestPoint(const std::string& path,int testPointID,int testPointIDCount,const std::string& kz)
{
	std::cout<<"==================ACS testpoint-reading start=================="<<std::endl;
	HANDLE fileHandle;
	//保存文件大小
	LARGE_INTEGER liFileSize;
	//成功读取的文件数据大小
	DWORD dwReadedSize;
	//累加计算已经读取的数据的大小
	LONGLONG liTotalRead=0;
	//文件数据缓存
	BYTE lpFileDataBuffer[320];
	std::string result = "";
	std::string filePath;
	if(testPointIDCount!=-1)
	{
		filePath = path+"\\"+std::to_string(testPointID)+"\\"+std::to_string(testPointIDCount);
	}
	else
	{
		filePath = path+"\\"+std::to_string(testPointID)+".tpcnt";
	}
	filePath.append(kz);
	std::cout<<"File Path : "<<filePath<<std::endl;
	fileHandle = CreateFileA
	(
		filePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,                   //以读方式打开
		FILE_SHARE_READ,               //可共享读
		NULL,                           //默认安全设置
		OPEN_EXISTING,                   //只打开已经存在的文件
		FILE_ATTRIBUTE_NORMAL,           //常规文件属性
		NULL
	);
	if(fileHandle==INVALID_HANDLE_VALUE)
	{
		std::cout<<"Failed to open file"<<std::endl;
		CloseHandle(fileHandle);
		return "";
	}
	if(!GetFileSizeEx(fileHandle,&liFileSize))
	{
		std::cout<<"Failed to get size of the file"<<std::endl;
		return "";
	}
	else
	{
		printf("Size of the file ：%d\n",liFileSize.QuadPart);
	}
	while(TRUE)
	{
		DWORD i;
		if(!ReadFile(fileHandle,lpFileDataBuffer,320,&dwReadedSize,NULL))               //不使用Overlapped
		{
			std::cout<<"Error when read the file"<<std::endl;
			break;
		}
		printf("Read %d bytes，is ：",dwReadedSize);
		for(i=0;i<dwReadedSize;i++)
		{
			printf("0x%x ",lpFileDataBuffer[i]);
		}
		result.append(reinterpret_cast<char*>(lpFileDataBuffer), dwReadedSize);
		printf("\n");
		liTotalRead+=dwReadedSize;
		if(liTotalRead==liFileSize.QuadPart)
		{
			printf("Finished reading the file\n");
			break;
		}
	}
	std::cout<<"Result : "<<std::endl<<result<<std::endl;
	CloseHandle(fileHandle);
	std::cout<<"==================ACS testpoint-reading end=================="<<std::endl;
	return result;
}
bool compile(const std::string& command)
{
	PROCESS_INFORMATION information;
	STARTUPINFO info;
	memset(&information,0,sizeof(information));
	memset(&info,0,sizeof(info));
	info.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	info.wShowWindow = SW_HIDE; 
	info.cb=sizeof(info);
	std::vector<char> cmdBuffer(command.begin(), command.end());
	cmdBuffer.push_back('\0');
	CreateProcessA(NULL,cmdBuffer.data(),NULL, NULL,TRUE,CREATE_NO_WINDOW,NULL, NULL, &info,&information );
	WaitForSingleObject(information.hProcess, INFINITE);
	DWORD exitCode;
	GetExitCodeProcess(information.hProcess, &exitCode);
	if(exitCode==0){return true;}
	return false;
}
void init(std::string* commandptr,std::string* tempStrptr,std::string* testPointStrptr)
{
	std::cout<<"==================ACS init start=================="<<std::endl;
	char currentDirectoryBuffer[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, currentDirectoryBuffer);
	std::cout<<"Current directory : "<<currentDirectoryBuffer<<std::endl;
	std::string tempStr = "";
	std::string workStr = "";
	std::string testPointStr = "";
	tempStr.append(currentDirectoryBuffer);
	tempStr.append("\\temp");
	testPointStr.append(currentDirectoryBuffer);
	testPointStr.append("\\testpoint");
	workStr.append(currentDirectoryBuffer);
	workStr.append("\\mingw64\\bin");
	std::cout<<"Temp directory : "<<tempStr<<std::endl;
	std::cout<<"Working directory : "<<workStr<<std::endl;
	std::cout<<"TestPoint directory : "<<testPointStr<<std::endl;
	SetCurrentDirectory(workStr.c_str());
	std::cout<<"Set current directory"<<std::endl;
	std::string command = "g++.exe "+tempStr+"\\source.cpp "+"-o "+tempStr+"\\source.exe -s -static";
	std::cout<<"Command : "<<command<<std::endl; 
	commandptr->append(command);
	tempStrptr->append(tempStr);
	testPointStrptr->append(testPointStr);
	std::cout<<"==================ACS init end=================="<<std::endl;
}
int handleRequest(TestingRequest request,std::string commandptr,std::string tempStrptr,std::string answer1)
{
	std::cout<<"====================================ACS testing start===================================="<<std::endl;
	if(compile(commandptr))
	{
		int testPointCount;
		try
		{
			testPointCount = std::stoi(getTestPoint(answer1,request.questionCount,-1,""));
			std::cout<<"Testpoint count : "<<testPointCount<<std::endl;
		}catch(const std::exception&)
		{
			return ILLEGAL_TESTING_POINT;
		}
		for(int i = 0 ; i< testPointCount ; i++)
		{
			std::cout<<"Testing point : "<<i<<std::endl;
			int timelimit,memlimit;
			try
			{
				memlimit = std::stoi(getTestPoint(answer1,request.questionCount,i,".memlimit"));
				timelimit = std::stoi(getTestPoint(answer1,request.questionCount,i,".tlimit"));
			}catch(const std::exception&)
			{
				return ILLEGAL_TESTING_POINT;
			}
			std::cout<<"Time limit : "<<timelimit<<std::endl;
			std::cout<<"Memory limit(MB) : "<<memlimit<<std::endl;
			int flag = execCommand(tempStrptr+"\\source.exe",getTestPoint(answer1,request.questionCount,i,".tpin"),getTestPoint(answer1,request.questionCount,i,".tpout"),timelimit,(SIZE_T)memlimit*1048576);
			if(flag!=ANSWER_CORRECT){return flag;}
		}
		std::string cmd = tempStrptr+"\\source.exe";
		DeleteFileA(cmd.c_str());
		std::cout<<"====================================ACS testing end===================================="<<std::endl;
		return ANSWER_CORRECT;
	}
	else
	{
		return COMPILE_ERROR;	
	}	
}
int main() 
{
	std::queue<TestingRequest> requests;
	std::string command = "",tempStr = "",testPointStr = "";
	init(&command,&tempStr,&testPointStr);
	requests.push(getRequest("",0));
	switch (handleRequest(requests.front(),command,tempStr,testPointStr)) 
	{
		case COMPILE_ERROR:
			std::cout<<"Compile Error !"<<std::endl;
			break;
		case ANSWER_CORRECT:
			std::cout<<"Answer Correct !"<<std::endl;
			break;
		case WRONG_ANSWER:
			std::cout<<"Wrong Answer !"<<std::endl;
			break;
		case ILLEGAL_TESTING_POINT:
			std::cout<<"Illegal Testing Point !"<<std::endl;
			break;
		case TIME_LIMIT_ERROR:
			std::cout<<"Time Limit Error !"<<std::endl;
			break;
		case MEMORY_LIMIT_ERROR:
			std::cout<<"Memory Limit Error !"<<std::endl;
		break;
		default:
			break;
	}
	Sleep(1000);
	system("pause");
	return 0;
}
