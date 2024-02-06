#pragma region header 

#undef UNICODE

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define SLEEP Sleep
    #define ms 1
    #define ISVALIDSOCKET(s) ((s) != INVALID_SOCKET)
    #define CLOSESOCKET(s) closesocket(s)
    #define GETSOCKETERRNO() (WSAGetLastError())
    #pragma comment (lib, "Ws2_32.lib")
#else
    #include <string.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <wchar.h>
    #include <netinet/tcp.h>
    #include <sys/select.h>
    #define INVALID_SOCKET -1
    #define NO_ERROR 0
    #define SOCKET_ERROR -1
    #define ZeroMemory(Destination,Length) memset((Destination),0,(Length))
    #define SLEEP usleep
    #define ms 1000
    inline void nopp() {}
    #define SOCKET int
    #define ISVALIDSOCKET(s) ((s) >= 0)
    #define CLOSESOCKET(s) close(s)
    #define GETSOCKETERRNO() (errno)
    #define BYTE uint8_t
    #define BOOL int
#endif

#include <stdio.h>
#include <stdlib.h>

#include "WolframLibrary.h"
#include "WolframIOLibraryFunctions.h"
#include "WolframNumericArrayLibrary.h"

#pragma endregion

#pragma region initialization 

DLLEXPORT mint WolframLibrary_getVersion() {
    //printf("[WolframLibrary_getVersion]\n\tlibrary version: %d\n\n", WolframLibraryVersion);
    return WolframLibraryVersion;
}

DLLEXPORT int WolframLibrary_initialize(WolframLibraryData libData) {
    #ifdef _WIN32
        int iResult; 
        WSADATA wsaData; 

        iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
        if (iResult != 0) {
            return LIBRARY_FUNCTION_ERROR;
        }
    #endif

    //printf("[WolframLibrary_initialize]\n\tinitialized\n\n"); 
    return LIBRARY_NO_ERROR; 
}

DLLEXPORT void WolframLibrary_uninitialize(WolframLibraryData libData) { 
    #ifdef _WIN32
        WSACleanup(); 
    #endif 
    SLEEP(1000 * ms);
    //printf("[WolframLibrary_uninitialize]\n\tuninitialized\n\n"); 

    return; 
}

#pragma endregion

#pragma region internal

typedef struct Server_st {
    SOCKET listenSocket;
    size_t clientsLength;
    size_t clientsLengthMax;
    size_t clientsLengthInvalid;
    SOCKET *clients;
    size_t bufferSize;
    BYTE *buffer;
}* Server;

void addClient(Server server, SOCKET client){
    server->clients[server->clientsLength] = client; 
    server->clientsLength++; 
    
    if (server->clientsLength == server->clientsLengthMax){
        server->clientsLengthMax *= 2; 
        server->clients = realloc(server->clients, server->clientsLengthMax * sizeof(SOCKET));
    }
    //printf("[addClient]\n\tadded new client with id = %d\n\n", (int)client); 
}

void removeInvalidClients(Server server){
    size_t j = 0; 
    for (size_t i = 0; i < server->clientsLength; i++){
        if (server->clients[i] > 0) {
            server->clients[j] = server->clients[i]; 
            j++; 
        }
    }
    
    server->clientsLength = j; 
    while ((2 * server->clientsLength < server->clientsLengthMax) && server->clientsLengthMax > 1) {
        server->clientsLengthMax = server->clientsLengthMax / 2;
        server->clients = realloc(server->clients, sizeof(SOCKET) * server->clientsLengthMax);
    }
}

typedef struct SocketListenerTaskArgs_st {
    WolframLibraryData libData; 
    Server server;
}* SocketListenerTaskArgs; 

int socketWrite(SOCKET socketId, BYTE *data, int dataLength, int bufferSize){ 
    int iResult; 
    int writeLength; 
    char *buffer; 
    int err;   
    SOCKET currentSoketIdBackup; 
    int timeoutMult = 2; 
 
    fd_set set;
    struct timeval socktimeout; 
    socktimeout.tv_sec = 6; 
    socktimeout.tv_usec = 0; 

    int rv; 
    FD_ZERO(&set);
    FD_SET(socketId, &set);

    //printf("[socketWrite]\n\twrite %d bytes to socket %d\n\n", dataLength, (int)socketId);

    for (int i = 0; i < dataLength; i += bufferSize) { 
        buffer = (char*)&data[i];  
        writeLength = dataLength - i > bufferSize ? bufferSize : dataLength - i;  

        if (select(socketId+1, NULL, &set, NULL, &socktimeout) == 1) {
            iResult = send(socketId, buffer, writeLength, 0); 
            if (iResult == SOCKET_ERROR) {
                err = GETSOCKETERRNO();  
                //printf("[socketWrite]\n\terror write %d bytes to socket %d on step %d error number = %d\n\n", dataLength, (int)socketId, i, err);
                if (err == 10035 || err == 35) { 
                    //printf("[socketWrite]\n\twrite paused on %d ms", timeoutMult);
                    SLEEP(timeoutMult * ms); 
                    i -= bufferSize; 
                    timeoutMult *= 2; 
                    if (timeoutMult > 2000) { 
                        return SOCKET_ERROR; 
                    } 
                } else { 
                    return SOCKET_ERROR; 
                } 
            } else { 
                timeoutMult = 2; 
            }
        } else {
            SLEEP(timeoutMult * ms); 
            i -= bufferSize; 
            timeoutMult *= 2; 
            if (timeoutMult > 1000) { 
                //printf("[socketWrite]\n\ttimeout error!\n\n");
                return SOCKET_ERROR; 
            } 
        }
    } 
 
    return dataLength; 
}

MNumericArray createByteArray(WolframLibraryData libData, BYTE *data, const mint dataLength){
    MNumericArray nArray;
    libData->numericarrayLibraryFunctions->MNumericArray_new(MNumericArray_Type_UBit8, 1, &dataLength, &nArray);
    memcpy((uint8_t*) libData->numericarrayLibraryFunctions->MNumericArray_getData(nArray), data, dataLength);
    return nArray;
}

typedef struct List_st
{
    int* data; 
    size_t length; 
    size_t capacity; 
}* List; 

List LIST(){
    List list = (List)malloc(sizeof(struct List_st));

    list->capacity = 1; 
    list->length = 0; 
    list->data = (int*)malloc(sizeof(int)); 

    return list; 
}

int listGet(List list, int i){
    return list->data[i]; 
}

void listSet(List list, int i, int item){
    list->data[i] = item; 
    return; 
}

void listAdd(List list, int item){
    if (list->length == list->capacity){
        list->capacity = list->capacity * 2; 
        list->data = (int*)realloc(list->data, list->capacity * sizeof(int)); 
    }
    list->data[list->length] = item;
    list->length = list->length + 1; 
    return; 
}

void listClear(List list){
    int j = 0; 
    for (int i = 0; i < list->length - 1; i++){
        if (list->data[i] > 0){
            list->data[j] = list->data[i]; 
            j++; 
        }
    }

    list->length = j; 

    if (2 * j <= list->capacity){
        list->capacity = list->capacity / 2; 
        list->data = (int*)realloc(list->data, sizeof(int) * list->capacity);
    }
    
    return; 
}

#pragma endregion

#pragma region socketOpen[host_String, port_String]: socketId_Integer 

DLLEXPORT int socketOpen(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    char* host = MArgument_getUTF8String(Args[0]);
    char* port = MArgument_getUTF8String(Args[1]);

    int iResult; 
    SOCKET listenSocket = INVALID_SOCKET; 
    struct addrinfo hints; 
    struct addrinfo *address = NULL; 
    int iMode = 1;
    int buffSize = 262144;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    iResult = getaddrinfo(host, port, &hints, &address);
    if (iResult != 0) {
        return LIBRARY_FUNCTION_ERROR;
    }

    listenSocket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (!ISVALIDSOCKET(listenSocket)) {
        freeaddrinfo(address);
        return LIBRARY_FUNCTION_ERROR;
    }

    iResult = (listenSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&iMode, sizeof(iMode)); 
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(listenSocket);
        return LIBRARY_FUNCTION_ERROR;
    }

    iResult = (listenSocket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&iMode, sizeof(iMode)); 
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(listenSocket);
        return LIBRARY_FUNCTION_ERROR;
    }

    iResult = (listenSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&buffSize, sizeof(buffSize)); 
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(listenSocket);
        return LIBRARY_FUNCTION_ERROR;
    }

    iResult = (listenSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&buffSize, sizeof(buffSize)); 
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(listenSocket);
        return LIBRARY_FUNCTION_ERROR;
    }

    iResult = bind(listenSocket, address->ai_addr, (int)address->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(listenSocket);
        return LIBRARY_FUNCTION_ERROR;
    }

    iResult = listen(listenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(listenSocket);
        return LIBRARY_FUNCTION_ERROR;
    }

    #ifdef _WIN32 
    iResult = ioctlsocket(listenSocket, FIONBIO, &iMode); 
    #else
    int flags = fcntl(listenSocket, F_GETFL, 0); 
    flags |= O_NONBLOCK;
    flags |= O_ASYNC; 
    iResult = fcntl(listenSocket, F_SETFL, flags, &iMode); 
    #endif

    //printf("[socketOpen]\n\tid: %d\n\thost: %s\n\tport: %s\n\n", listenSocket, host, port);

    if (iResult != NO_ERROR) {
        //printf("[socketOpen]\n\terror! iResult = %d and error number = %d\n\n", iResult, GETSOCKETERRNO());
    }

    freeaddrinfo(address);

    MArgument_setInteger(Res, listenSocket);
    return LIBRARY_NO_ERROR; 
}

#pragma endregion

#pragma region socketClose[socketId_Integer]: socketId_Integer 

DLLEXPORT int socketClose(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    SOCKET socketId = MArgument_getInteger(Args[0]);
    //printf("[socketClose]\n\tid: %d\n\n", socketId);
    MArgument_setInteger(Res, CLOSESOCKET(socketId));
    return LIBRARY_NO_ERROR; 
}

#pragma endregion

#pragma region socketListen[socketid_Integer, bufferSize_Integer]: taskId_Integer 

static void socketListenerTask(mint taskId, void* vtarg)
{
    SocketListenerTaskArgs targ = (SocketListenerTaskArgs)vtarg;
    Server server = targ->server;
	WolframLibraryData libData = targ->libData;

    int iResult;
    SOCKET clientSocket = INVALID_SOCKET;
    char *buffer = (char*)malloc(server->bufferSize * sizeof(char));
    mint dims[1];
    MNumericArray data;
	DataStore ds;

    time_t readTime = time(NULL); 
    BOOL sleepMode = True;
    
	while(libData->ioLibraryFunctions->asynchronousTaskAliveQ(taskId))
	{
        if (sleepMode){
            SLEEP(10 * ms);
            removeInvalidClients(server);
        }

        clientSocket = accept(server->listenSocket, NULL, NULL); 
        if (ISVALIDSOCKET(clientSocket)) {
            addClient(server, clientSocket);
            sleepMode = False; 
        }

        sleepMode = True; 
        for (int i = 0; i < server->clientsLength; i++) {
            if (server->clients[i] != INVALID_SOCKET) {
                iResult = recv(server->clients[i], buffer, server->bufferSize, 0); 
                if (iResult > 0) {
                    //printf("[socketListenerTask]\n\treceived %d bytes from socket %d\n\n", iResult, (int)(server->clients[i]));
                    sleepMode *= False; 
                    dims[0] = iResult;
                    libData->numericarrayLibraryFunctions->MNumericArray_new(MNumericArray_Type_UBit8, 1, dims, &data); 
                    memcpy(libData->numericarrayLibraryFunctions->MNumericArray_getData(data), buffer, iResult);
                    ds = libData->ioLibraryFunctions->createDataStore();
                    libData->ioLibraryFunctions->DataStore_addInteger(ds, server->listenSocket);
                    libData->ioLibraryFunctions->DataStore_addInteger(ds, server->clients[i]);
                    libData->ioLibraryFunctions->DataStore_addMNumericArray(ds, data);
                    libData->ioLibraryFunctions->raiseAsyncEvent(taskId, "Received", ds);
                } else if (iResult == 0) {
                    server->clients[i] = INVALID_SOCKET;
                }
            }
        }
	}

    for (int i = 0; i < server->clientsLength; i++) {
        CLOSESOCKET(server->clients[i]);
    }

    free(targ); 
    free(server->clients);
    free(buffer);
}

DLLEXPORT int socketListen(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    SOCKET listenSocket = MArgument_getInteger(Args[0]);
    int bufferSize = MArgument_getInteger(Args[1]);
    
    mint taskId;
    SOCKET *clients = (SOCKET*)malloc(4 * sizeof(SOCKET));
    Server server = (Server)malloc(sizeof(struct Server_st));

    server->listenSocket=listenSocket;
    server->clients=clients;
    server->clientsLength=0;
    server->clientsLengthMax=4;
    server->bufferSize=bufferSize;

    SocketListenerTaskArgs threadArg = (SocketListenerTaskArgs)malloc(sizeof(struct SocketListenerTaskArgs_st));
    threadArg->libData=libData; 
    threadArg->server=server; 
    taskId = libData->ioLibraryFunctions->createAsynchronousTaskWithThread(socketListenerTask, threadArg);

    MArgument_setInteger(Res, taskId); 
    return LIBRARY_NO_ERROR; 
}

#pragma endregion

#pragma region socketListenerTaskRemove[taskId_Integer]: taskId_Integer 

DLLEXPORT int socketListenerTaskRemove(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    mint taskId = MArgument_getInteger(Args[0]);
    MArgument_setInteger(Res, libData->ioLibraryFunctions->removeAsynchronousTask(taskId));
    return LIBRARY_NO_ERROR;
}

#pragma endregion

#pragma region socketConnect[host_String, port_String]: socketId_Integer 

DLLEXPORT int socketConnect(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    char *host = MArgument_getUTF8String(Args[0]);
    char *port = MArgument_getUTF8String(Args[1]);

    int iResult; 
    int iMode = 1; 
    SOCKET connectSocket = INVALID_SOCKET; 
    struct addrinfo *address = NULL; 
    struct addrinfo hints; 

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    iResult = getaddrinfo(host, port, &hints, &address);
    if (iResult != 0){
        return LIBRARY_FUNCTION_ERROR; 
    }

    connectSocket = socket(address->ai_family, address->ai_socktype, address->ai_protocol); 
    if (connectSocket == INVALID_SOCKET){
        freeaddrinfo(address); 
        return LIBRARY_FUNCTION_ERROR; 
    }

    iResult = connect(connectSocket, address->ai_addr, (int)address->ai_addrlen);
    freeaddrinfo(address);
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(connectSocket); 
        connectSocket = INVALID_SOCKET;
        return LIBRARY_FUNCTION_ERROR;
    }

    #ifdef _WIN32 
    iResult = ioctlsocket(connectSocket, FIONBIO, &iMode); 
    #else
    iResult = fcntl(connectSocket, O_NONBLOCK, &iMode); 
    #endif

    //if (iResult != NO_ERROR) {
    //print
    //}

    MArgument_setInteger(Res, connectSocket); 
    return LIBRARY_NO_ERROR;
}

#pragma endregion

#pragma region socketBinaryWrite[socketId_Integer, data: ByteArray[<>], dataLength_Integer, bufferLength_Integer]: socketId_Integer 

DLLEXPORT int socketBinaryWrite(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    SOCKET clientId = MArgument_getInteger(Args[0]); 
    MNumericArray mArr = MArgument_getMNumericArray(Args[1]); 

    int iResult;
    BYTE *data = (BYTE *)libData->numericarrayLibraryFunctions->MNumericArray_getData(mArr); 
    int dataLength = MArgument_getInteger(Args[2]); 
    int bufferSize = MArgument_getInteger(Args[3]); 
    
    iResult = socketWrite(clientId, data, dataLength, bufferSize); 
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(clientId);
        MArgument_setInteger(Res, GETSOCKETERRNO()); 
        return LIBRARY_FUNCTION_ERROR; 
    }
    
    MArgument_setInteger(Res, clientId);
    return LIBRARY_NO_ERROR;
}

#pragma endregion

#pragma region socketWriteString[socketId_Integer, data_String, dataLength_Integer, bufferSize_Integer]: socketId_Integer 

DLLEXPORT int socketWriteString(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    int iResult; 
    SOCKET socketId = MArgument_getInteger(Args[0]); 
    char* data = MArgument_getUTF8String(Args[1]); 
    int dataLength = MArgument_getInteger(Args[2]); 
    int bufferSize = MArgument_getInteger(Args[3]); 
    
    iResult = socketWrite(socketId, data, dataLength, bufferSize); 
    if (iResult == SOCKET_ERROR) {
        CLOSESOCKET(socketId);
        MArgument_setInteger(Res, GETSOCKETERRNO()); 
        //printf("[socketWriteString]\n\terror number = %d", GETSOCKETERRNO()); 
        return LIBRARY_FUNCTION_ERROR; 
    }
  
    MArgument_setInteger(Res, socketId);
    return LIBRARY_NO_ERROR;
}

#pragma endregion

#pragma region socketReadyQ[socketId_Integer]: readyQ: True | False 

DLLEXPORT int socketReadyQ(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    SOCKET socketId = MArgument_getInteger(Args[0]); 
    
    int iResult; 
    BYTE *buffer = (BYTE *)malloc(sizeof(BYTE)); 
    
    iResult = recv(socketId, buffer, 1, MSG_PEEK);
    if (iResult == SOCKET_ERROR){
        MArgument_setBoolean(Res, False); 
    } else {
        MArgument_setBoolean(Res, True); 
    }

    free(buffer);
    return LIBRARY_NO_ERROR;
}

#pragma endregion

#pragma region socketReadMessage[socketId_Integer, bufferSize_Integer]: ByteArray[<>] 

DLLEXPORT int socketReadMessage(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res){
    SOCKET socketId = MArgument_getInteger(Args[0]);
    int bufferSize = MArgument_getInteger(Args[1]);
    
    BYTE *buffer = (BYTE*)malloc(bufferSize * sizeof(BYTE));
    int iResult;
    int length = 0;

    iResult = recv(socketId, buffer, bufferSize, 0);
    if (iResult > 0) {
        MArgument_setMNumericArray(Res, createByteArray(libData, buffer, iResult));
    } else {
        return LIBRARY_FUNCTION_ERROR;
    }

    return LIBRARY_NO_ERROR; 
}

#pragma endregion

#pragma region socketPort[socketId_Integer]: port_Integer 

DLLEXPORT int socketPort(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) {
    SOCKET socketId = MArgument_getInteger(Args[0]); 
    struct  sockaddr_in sin;
    int port;
    int addrlen = sizeof(sin);

    getsockname(socketId, (struct sockaddr *)&sin, &addrlen);
    port = ntohs(sin.sin_port); 

    MArgument_setInteger(Res, port);
    return LIBRARY_NO_ERROR; 
}

#pragma endregion

#pragma region socketHostname[socketId_Integer]: hostname_String 

DLLEXPORT int socketHostname(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) {
    SOCKET socketId = MArgument_getInteger(Args[0]); 
    struct  sockaddr_in sin;
    char str[INET_ADDRSTRLEN]; 
    int addrlen = sizeof(sin);

    getsockname(socketId, (struct sockaddr *)&sin, &addrlen);
    struct in_addr ip = sin.sin_addr;
    inet_ntop(AF_INET, &ip, str, INET_ADDRSTRLEN);

    MArgument_setUTF8String(Res, str);
    return LIBRARY_NO_ERROR;  
}

#pragma endregion

#pragma region socketClients[socketId_Integer]: clients: {___Integer} 

DLLEXPORT int socketClients(WolframLibraryData libData, mint Argc, MArgument *Args, MArgument Res) {
    SOCKET socketId = MArgument_getInteger(Args[0]); 
    MTensor clients; 
    int dims[1]; 
    dims[0] = 0; 
    libData->MTensor_new(MType_Integer, 1, dims, &clients); 
    MArgument_setMTensor(Res, clients); 
    return LIBRARY_NO_ERROR;  
}

#pragma endregion
