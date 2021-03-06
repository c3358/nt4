/*++

Copyright (c) 1992 Microsoft Corporation

Module Name:

    dgudpc.cxx

Abstract:

    This is the UDP datagram client dll.

Author:

    Dave Steckler (davidst) 15-Mar-1993

Revision History:

--*/


#include "sysinc.h"
#include "limits.h"
#include "rpc.h"
#include "rpcdcep.h"
#include "rpctran.h"
#include "rpcerrp.h"
#include "winsock.h"
#include "windows.h"
#include "callback.h"

#define errno _FakeErrno
int _FakeErrno;

extern void (_far pascal _far *DllTermination)(void);
extern atoi(char *);

#define ERROR_SEM_TIMEOUT RPC_P_TIMEOUT
#define MAXENDPOINT_SIZE 6

/*
  These are Transport Specific ENDPOINTS and ADDRESS
  Runtime has allocated a chuck of memory and as far as runtime is
  concerned this is opaque data that transport uses in transport
  specific way!
*/


typedef struct {
    int                 Socket;
    fd_set              Set;
} DG_UDP_ENDPOINT;


typedef DG_UDP_ENDPOINT * PDG_UDP_ENDPOINT;

typedef struct {
    struct sockaddr_in ServerAddress;
    BOOL               ServerLookupFailed;
} DG_UDP_ADDRESS;

typedef DG_UDP_ADDRESS * PDG_UDP_ADDRESS;

#define ByteSwapShort(Value) \
    Value = (  (((Value) & 0x00FF) << 8) \
             | (((Value) & 0xFF00) >> 8))



/*
   Following Macros and structs are needed for Tower Stuff
*/

#pragma pack(1)
#define UDP_TRANSPORTID      0x08
#define UDP_TRANSPORTHOSTID  0x09
#define UDP_TOWERFLOORS         5
#define UDP_IP_EP            "135"

#define UDP_PROTSEQ          "ncadg_ip_udp"

typedef struct _FLOOR_234 {
   unsigned short ProtocolIdByteCount;
   unsigned char FloorId;
   unsigned short AddressByteCount;
   unsigned char Data[2];
} FLOOR_234, PAPI * PFLOOR_234;


#define NEXTFLOOR(t,x) (t)((unsigned char PAPI *)x +((t)x)->ProtocolIdByteCount\
                                        + ((t)x)->AddressByteCount\
                                        + sizeof(((t)x)->ProtocolIdByteCount)\
                                        + sizeof(((t)x)->AddressByteCount))

/*
  End of Tower Stuff!
*/


#pragma pack()

RPC_STATUS
MapStatus(
    int SocketError,
    RPC_STATUS Default
    );



RPC_STATUS RPC_ENTRY
AssignLocalEndpoint(
    IN void * Endpoint
    )

/*++

Routine Description:

    Ask transport for a new endpoint.

Arguments:


Return Value:

    RPC_S_OK

--*/

{

    BOOL           SetSocketOptions;
    struct sockaddr_in    Client;
    int            Error;
    WSADATA        Data;
    int            Status;
    PDG_UDP_ENDPOINT TransportEndpoint = (PDG_UDP_ENDPOINT) Endpoint;

    Status = WSAStartup(
        0x0101,         // version required
        &Data
        );

    if (Status != 0)
        {
        return RPC_S_OUT_OF_MEMORY;
        }
    //
    // Create a socket.
    //
    TransportEndpoint->Socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (TransportEndpoint->Socket < 0)
        {
        Error = WSAGetLastError();
        return (RPC_S_OUT_OF_MEMORY);
        }

    //Enable broadcasts by default .. on this socket
    //We may change this later - to do broadcasts by demand
    SetSocketOptions = TRUE;
    if (setsockopt(
          TransportEndpoint->Socket,
          SOL_SOCKET,
          SO_BROADCAST,
          (char *)&SetSocketOptions,
          sizeof(SetSocketOptions))      != 0)
        {
        closesocket(TransportEndpoint->Socket);
        return(RPC_S_CALL_FAILED_DNE);
        }

    /*
     Dos sockets bug ? - Must bind the socket
    */
    memset((char *)&Client, 0, sizeof(Client));
    Client.sin_family = AF_INET;
    Client.sin_addr.s_addr = INADDR_ANY;

    if (bind(TransportEndpoint->Socket, (struct sockaddr FAR *)&Client,
                                                          sizeof(Client)))
       {
       closesocket(TransportEndpoint->Socket);
       return(RPC_S_CALL_FAILED_DNE);
       }

    FD_ZERO(&(TransportEndpoint->Set));
    FD_SET(TransportEndpoint->Socket, &(TransportEndpoint->Set));

    return(RPC_S_OK);
}


RPC_STATUS RPC_ENTRY
FreeLocalEndpoint(
    IN void * Endpoint
    )

/*++

Routine Description:

    Frees an endpoint

Arguments:


Return Value:

    RPC_S_OK

--*/

{
    //We wont free the memory as the runtime will do that
    //We just do the transport related stuff

    PDG_UDP_ENDPOINT TransportEndpoint = (PDG_UDP_ENDPOINT)Endpoint;

    closesocket(TransportEndpoint->Socket);
    WSACleanup();
    return(RPC_S_OK);
}


RPC_STATUS RPC_ENTRY
RegisterServerAddress(
    IN void __RPC_FAR *                       pClientCall,
    IN RPC_CHAR __RPC_FAR *                   pServer,
    IN RPC_CHAR __RPC_FAR *                   pEndpoint,
    OUT void __RPC_FAR * __RPC_FAR *          ppTransAddress
    )

/*++

Routine Description:

    Registers a new call with the transport. This informs the transport that
    data is about to be sent and received on this address to and from
    the server/endpoint. This routine returns a 'transport address' through
    which the sending and receiving will be accomplished.

    This routine serves mainly as a psuedo-constructor for a
    DG_UDP_CLIENT_TRANSPORT object, where all the real work occurs.

Arguments:

    pClientCall - A pointer to the protocol's DG_CCALL object for this call.
        This is defined as a 'void *' instead of a PDG_CCALL because we don't
        want to have to include (and link in) all the stuff associated
        with DG_CCALL (including DCE_BINDING, BINDING_HANDLE, MESSAGE_OBJECT,
        etc.)

    pServer - Name of the server we are talking with.

    pEndpoint - Endpoint on that server.

    ppTransAddress - Where to place a pointer to a new transport address
        which the protocol will use to identify the socket
        we are using.

Return Value:

    RPC_S_OK
    RPC_S_OUT_OF_MEMORY
    <return from construction of DG_UDP_CLIENT_TRANS_ADDRESS>

--*/

{
    RPC_STATUS      Status;
    int             i;
    struct hostent  *pHostEntry;
    unsigned long   HostAddr;
    int             Endpoint;
    PDG_UDP_ADDRESS pdgAddress = (PDG_UDP_ADDRESS) *ppTransAddress ;
    WSADATA                     Data;

    if (*ppTransAddress == NULL)
        {
        return RPC_S_OUT_OF_MEMORY;
        }

    Status = WSAStartup(
        0x0101,         // version required
        &Data
        );

    if (Status != 0)
        {
        return RPC_S_OUT_OF_MEMORY;
        }

    pdgAddress->ServerLookupFailed = FALSE;

    //
    // convert the endpoint to a number.
    //
    Endpoint = atoi(pEndpoint);

    //
    // Get the address of the host.
    //
    if ((pServer == NULL) || (*pServer == '\0'))
        {
        pServer = "127.0.0.1";
        }

    HostAddr = inet_addr(pServer);
    if (HostAddr == -1)
        {
        pHostEntry = gethostbyname(pServer);
        if (pHostEntry == 0)
            {
            pdgAddress->ServerLookupFailed = TRUE;
            }
        else
            {
            HostAddr = *(unsigned long *)pHostEntry->h_addr;
            }
        }

    pdgAddress->ServerAddress.sin_family = AF_INET;
    pdgAddress->ServerAddress.sin_port = htons(Endpoint);

    if (pdgAddress->ServerLookupFailed == FALSE)
        {
        RpcpMemoryCopy(
                  (char *) &(pdgAddress->ServerAddress.sin_addr.s_addr),
                  (char *) &HostAddr,
                  sizeof (unsigned long)
                  );
        }

    return RPC_S_OK;
}




RPC_STATUS RPC_ENTRY
DeregisterServerAddress(
    IN void * pTransAddress
    )

/*++

Routine Description:

    This routine serves as a psuedo-destructor for a DG_UDP_CLIENT_TRANSPORT
    object. It frees up a socket.

Arguments:

    pTransAddress - Address to deregister.

Return Value:

    RPC_S_OK

--*/

{
    WSACleanup();
    return RPC_S_OK;
}



RPC_STATUS RPC_ENTRY
SendToServer(
    IN void *                       TransportEndpoint,
    IN void *                       Buffer,
    IN unsigned long                BufferLength,
    IN BOOL                         Broadcast,
    IN void *                       TransportAddress
    )

/*++

Routine Description:

    Sends a packet on the network through the transport address associated
    with the passed packet.

Arguments:

    pPack - Packet to send.
    Broadcast - Whether to broadcast or not.

Return Value:

    RPC_S_OK
    <return value from MapStatusCode>

--*/

{
    PDG_UDP_ADDRESS pTransAddress = (PDG_UDP_ADDRESS)TransportAddress;
    int             SockStatus;
    int             Socket = ((PDG_UDP_ENDPOINT)TransportEndpoint)->Socket;

    //
    // Send the data on the net.
    //

    if ((Broadcast == FALSE) && (pTransAddress->ServerLookupFailed == TRUE))
        {
        return(RPC_S_SERVER_UNAVAILABLE);
        }

    if (Broadcast)
        {
        struct sockaddr_in  ServerAddress;
        unsigned long Tmp = INADDR_BROADCAST;

        ServerAddress.sin_family = AF_INET;
        ServerAddress.sin_port = pTransAddress->ServerAddress.sin_port;
        RpcpMemoryCopy((char *) &ServerAddress.sin_addr.s_addr,
                         (char *) &Tmp,
                         sizeof(Tmp));

        SockStatus = sendto(
            Socket,
            (char *)Buffer,
            (int)BufferLength,
            0,
            (struct sockaddr *)&ServerAddress,
            sizeof(ServerAddress)
            );

        }
    else
        {
        SockStatus = sendto(
            Socket,
            (char *)Buffer,
            (int)BufferLength,
            0,
            (struct sockaddr *)&(pTransAddress->ServerAddress),
            sizeof(pTransAddress->ServerAddress)
            );

        }

    if (SockStatus == BufferLength)
        {
        return RPC_S_OK;
        }
    else
        {
#ifdef DEBUGRPC
        _asm { int 3 };
#endif
        SockStatus = WSAGetLastError();

        // UDP Problem ? May be - just hack around it for the timebeing
        //
        if (SockStatus == WSAEWOULDBLOCK)
           {
           return RPC_S_OK;
           }
        return MapStatus(SockStatus, RPC_P_SEND_FAILED);
        }

}


RPC_STATUS RPC_ENTRY
ReceivePacket(
    IN void *               TransportEndpoint,
    IN void *               Buffer,
    IN unsigned long *      BufferLength,
    IN unsigned long        Timeout,
    IN void *               SenderAddress
    )

/*++

Routine Description:

    Receives a packet from the network.

Arguments:

    pPack - Packet to receive into.
    Timeout - Timeout in seconds.

Return Value:

    RPC_S_OK
    ERROR_SEM_TIMEOUT
    <return from WaitForSingleObject or MapStatusCode>

--*/

{

    PDG_UDP_ADDRESS  pTransAddress = (PDG_UDP_ADDRESS)SenderAddress;
    PDG_UDP_ENDPOINT Endpoint = (PDG_UDP_ENDPOINT)TransportEndpoint;

    int             SockStatus;
    int             BytesReceived;
    int             DummyLen=sizeof(struct sockaddr_in);
    int             Length;
    struct timeval  TimeoutVal;


    TimeoutVal.tv_sec = Timeout;
    TimeoutVal.tv_usec = 0;

    //
    // Wait for our socket to be receivable.
    //

    SockStatus = select(
        1,
        &(Endpoint->Set),
        0,
        0,
        &TimeoutVal
        );

    if (SockStatus == 0)
        {
        FD_SET(Endpoint->Socket, &(Endpoint->Set));
        return ERROR_SEM_TIMEOUT;
        }

    if (SockStatus == -1)
        {
        SockStatus = WSAGetLastError();

#ifdef DEBUGRPC
        _asm int 3
#endif
        }

    //
    // Receive something on our socket.
    //
#ifdef DEBUGRPC
    if (*BufferLength > INT_MAX)
        {
        _asm int 3
        }
#endif

    Length = (int) *BufferLength;

    BytesReceived = recvfrom(
            Endpoint->Socket,
            (char *)Buffer,
            Length,
            0,
            SenderAddress,
            &DummyLen
            );

    //
    // Did we get something?
    //
    *BufferLength = BytesReceived;

    if ((BytesReceived < 0 ) || (BytesReceived == 0))
        {
        SockStatus = WSAGetLastError();

        if (   (SockStatus == WSAEWOULDBLOCK)
            || (SockStatus == 0 )
           )
           {
           // UDP Problem ? May be - just hack around it for the timebeing
           //
           return RPC_P_TIMEOUT;
           }

#ifdef DEBUGRPC
        if (SockStatus != WSAEMSGSIZE)
            {
            _asm { int 3 };
            }
#endif
        return MapStatus(SockStatus, RPC_P_TIMEOUT);
        }

      return RPC_S_OK;


}



RPC_STATUS RPC_ENTRY
Cleanup(
    void
    )
{
  /*
  WSACleanup();
  */
  return (RPC_S_OK);
}



#pragma pack(1)
RPC_STATUS RPC_ENTRY
ClientTowerConstruct(
     IN  char PAPI * Endpoint,
     IN  char PAPI * NetworkAddress,
     OUT unsigned short PAPI * Floors,
     OUT unsigned long  PAPI * ByteCount,
     OUT unsigned char PAPI * PAPI * Tower,
     IN  char PAPI * Protseq
    )
/*++


Routine Description:

    This function constructs upper floors of DCE tower from
    the supplied endpoint and network address. It returns #of floors
    [lower+upper] for this protocol/transport, bytes in upper floors
    and the tower [floors 4,5]

Arguments:

    Endpoint- A pointer to string representation of Endpoint

    NetworkAddress - A pointer to string representation of NW Address

    Floors - A pointer to #of floors in the tower

    ByteCount - Size of upper floors of tower.

    Tower - The constructed tower returmed - The memory is allocated
            by  the routine and caller will have to free it.

Return Value:

    RPC_S_OK

    RPC_S_OUT_OF_MEMORY - There is no memory to return the constructed
                          Tower.
--*/
{

  unsigned long TowerSize, * HostId;
  unsigned short * Port;
  PFLOOR_234 Floor;

  if (Protseq);

  *Floors = UDP_TOWERFLOORS;
  TowerSize  =  6; /*Endpoint = 2 bytes, HostId = 4 bytes*/

  TowerSize += 2*sizeof(FLOOR_234) - 4;

  if ((*Tower = (unsigned char PAPI*) I_RpcAllocate((unsigned int)
                                                    (*ByteCount = TowerSize)))
           == NULL)
     {
       return (RPC_S_OUT_OF_MEMORY);
     }

  Floor = (PFLOOR_234) *Tower;
  Floor->ProtocolIdByteCount = 1;
  Floor->FloorId = (unsigned char)(UDP_TRANSPORTID & 0xFF);
  Floor->AddressByteCount = 2;
  Port  = (unsigned short *) &Floor->Data[0];
  if (Endpoint == NULL || *Endpoint == '\0')
     {
        Endpoint = UDP_IP_EP;
     }

  *Port = htons ( atoi (Endpoint));

  //Onto the next floor
  Floor = NEXTFLOOR(PFLOOR_234, Floor);
  Floor->ProtocolIdByteCount = 1;
  Floor->FloorId = (unsigned char)(UDP_TRANSPORTHOSTID & 0xFF);
  Floor->AddressByteCount = 4;

  HostId = (unsigned long *)&Floor->Data[0];

  if ((NetworkAddress) && (*NetworkAddress))
     {
       *HostId = inet_addr((char *) NetworkAddress);
     }
  else
     {
       *HostId = 0;
     }

  return(RPC_S_OK);
}



RPC_STATUS RPC_ENTRY
ClientTowerExplode(
     IN unsigned char PAPI * Tower,
     OUT char PAPI * PAPI * Protseq,
     OUT char PAPI * PAPI * Endpoint,
     OUT char PAPI * PAPI * NetworkAddress
    )
{
/*++


Routine Description:

    This function takes the protocol/transport specific floors
    and returns Protseq, Endpoint and NwAddress

    Note: Since ther is no need to return NW Address, currently
    nothing is done for NW Address.

Arguments:

    Tower - The DCE tower, upper floors

    Protseq - Protocol Sequence returned- memory is allocated by the
              routine and caller will have to free using I_RpcFree

    Endpoitn- Endpoint returned- memory is allocated by the
              routine and caller will have to free using I_RpcFree

    NWAddress- Nothing is done here - just incase we need it later

Return Value:

    RPC_S_OK

    RPC_S_OUT_OF_MEMORY - There is no memory to return the constructed
                          Tower.
--*/
  PFLOOR_234 Floor = (PFLOOR_234) Tower;
  RPC_STATUS Status = RPC_S_OK;
  unsigned short portnum,  *Port;

  if (Protseq != NULL)
    {
      *Protseq = (char PAPI *) I_RpcAllocate(strlen(UDP_PROTSEQ) + 1);
      if (*Protseq == NULL)
        Status = RPC_S_OUT_OF_MEMORY;
      else
        memcpy(*Protseq, UDP_PROTSEQ, strlen(UDP_PROTSEQ) + 1);
    }

  if ((Endpoint == NULL) || (Status != RPC_S_OK))
    {
      return (Status);
    }

  *Endpoint  = (char PAPI *) I_RpcAllocate(6);  //Ports are all <64K [5 decimal dig +1]
  if (*Endpoint == NULL)
    {
      Status = RPC_S_OUT_OF_MEMORY;
    }
  else
   {
     Port = (unsigned short *)&Floor->Data[0];
     portnum = *Port;
     _itoa(ByteSwapShort(portnum), *Endpoint, 10);
   }

 return(Status);
}

#pragma pack()


RPC_STATUS RPC_ENTRY
QueryClientEndpoint
    (
    IN  void PAPI *     pOriginalEndpoint,
    OUT RPC_CHAR PAPI * pClientEndpoint
    )
{
    struct sockaddr_in PAPI * pSockAddr =
            (struct sockaddr_in PAPI *) pOriginalEndpoint;
    unsigned long NativeSocket = ntohs(pSockAddr->sin_port);

    _ultoa(NativeSocket, pClientEndpoint, 10);

    return RPC_S_OK;
}


RPC_STATUS RPC_ENTRY
SetBufferLength(
    IN void PAPI * Endpoint,
    IN unsigned    Length
    )
{
    DG_UDP_ENDPOINT __RPC_FAR * pInfo = (DG_UDP_ENDPOINT __RPC_FAR *) Endpoint;
    int SockStatus;

    SockStatus = setsockopt(pInfo->Socket,
                            SOL_SOCKET,
                            SO_RCVBUF,
                            (char *) &Length,
                            sizeof(Length)
                            );

    if (SockStatus == SOCKET_ERROR)
        {
        return RPC_S_OUT_OF_MEMORY;
        }

    return RPC_S_OK;
}


RPC_STATUS RPC_ENTRY
InqBufferLength(
    IN void     __RPC_FAR * Endpoint,
    IN unsigned __RPC_FAR * Length
    )
{
    DG_UDP_ENDPOINT __RPC_FAR * pInfo = (DG_UDP_ENDPOINT __RPC_FAR *) Endpoint;
    int SockStatus;
    int OptionDataLength = sizeof(unsigned);

    SockStatus = getsockopt(pInfo->Socket,
                            SOL_SOCKET,
                            SO_RCVBUF,
                            (char __RPC_FAR *) Length,
                            &OptionDataLength
                            );

    if (SockStatus == SOCKET_ERROR)
        {
        return RPC_S_OUT_OF_MEMORY;
        }

    return RPC_S_OK;
}


RPC_STATUS RPC_ENTRY
BeginCall(
    IN void     __RPC_FAR * OpaqueEndpoint,
    IN void     __RPC_FAR * Connection
    )
{
    return RPC_S_OK;
}

void RPC_ENTRY
EndCall(
    IN void     __RPC_FAR * OpaqueEndpoint
    )
{
}

DG_RPC_CLIENT_TRANSPORT_INFO TransInfo =
{
    RPC_TRANSPORT_INTERFACE_VERSION,
    UDP_TRANSPORTID,

    ClientTowerConstruct,
    ClientTowerExplode,

    sizeof(DG_UDP_ADDRESS),
    sizeof(DG_UDP_ENDPOINT),
    MAXENDPOINT_SIZE,
    0,

    0,
    ReceivePacket,
    SendToServer,
    RegisterServerAddress,
    DeregisterServerAddress,
    AssignLocalEndpoint,
    FreeLocalEndpoint,
    QueryClientEndpoint,
    SetBufferLength,
    InqBufferLength,

    BeginCall,
    EndCall,

    1024,
    1472,
    1472,

    1472,

    0
};

void __far __pascal MyWep();



DG_RPC_CLIENT_TRANSPORT_INFO * RPC_ENTRY
TransportLoad(
    RPC_CHAR * pProtocolSequence,
    RPC_CLIENT_RUNTIME_INFO PAPI * ClientRuntimeInfo
    )

/*++

Routine Description:

    This routine is the "psuedo constructor" for the client transport object.
    This is the exported entry point into this dll.

Arguments:

    pProtocolSequence - The protocol sequence we're running on.

Return Value:

    Pointer to a DG_UDP_CLIENT_TRANSPORT if successful, otherwise NULL.


--*/

{
    RPC_STATUS                  Status;
    WSADATA                     Data;
    unsigned    BufferLength;
    int         OptionDataLength;
    SOCKET      Socket;

    //
    // Initialize our network.
    //

    Status = WSAStartup(
        0x0101,         // version required
        &Data
        );

    if (Status != 0)
        {
        return 0;
        }

    //
    // Create a socket and determine the socket receive buffer length.
    //
    Socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (Socket < 0)
        {
        WSACleanup();
        return 0;
        }

    OptionDataLength = sizeof(BufferLength);;

    Status = getsockopt(Socket,
                        SOL_SOCKET,
                        SO_RCVBUF,
                        (char __RPC_FAR *) &BufferLength,
                        &OptionDataLength
                        );

    closesocket(Socket);

    if (Status == SOCKET_ERROR)
        {
        WSACleanup();
        return 0;
        }

    TransInfo.DefaultBufferLength = BufferLength;

    RpcRuntimeInfo = ClientRuntimeInfo;
    AsyncCallComplete = RpcRuntimeInfo->AsyncCallComplete;

    DllTermination = MyWep;

    return (&TransInfo);
}



void __far __pascal
MyWep(
    )
{
    if (0 != GetModuleHandle("WINSOCK"))
        {
        WSACleanup();
        }
}


RPC_STATUS
MapStatus(
    int SocketError,
    RPC_STATUS Default
    )

/*++

Routine Description:

    Maps a winsock return value into a RPC_STATUS.

Arguments:

    ErrorCode - Input error code.

Return Value:

    mapped status code

--*/

{
    RPC_STATUS  Status;

    switch (SocketError)
        {
        case 0:
            {
            Status = RPC_S_OK;
            break;
            }

        case WSAETIMEDOUT:
            {
            Status = RPC_P_TIMEOUT;
            break;
            }

        case WSAENOBUFS:
            {
            Status = RPC_S_OUT_OF_MEMORY;
            break;
            }

        case WSAEMSGSIZE:
            {
            Status = RPC_P_OVERSIZE_PACKET;
            break;
            }

        default:
            {
            Status = Default;
            }
        }
    return Status;
}


