#include <iostream>
#include <ctime> 
#include <thread>
#include <vector>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sstream>
#include <string>
   
using namespace std;
   
int packageLen = 10;
int bufferSize = 16;
long int timeOut = 30;
int SERVERPORT;
string EXITCALL = "BYE";
char *package = new char[4];
char buffer[50];
int sockfd;
struct sockaddr_in servaddr, cliaddr;
       
int n;
unsigned int len;

//to be used in parsing the checksum into 4 chars
char Clamp(int a)
{
	if(a < 0) return 0;
	if(a > 127) return 127;
	return a;
}


class Data
{
    public : 
    	//sequence number - out of limit
        short serial;
        
        //checksum of real data parsed into 4 chars
        char cs[4];
        
        //4 chars of real data each package
        char c[4];
        Data(char c[4],short serial,char cs[4])
        {
        this->c[0] = c[0];this->c[1] = c[1];this->c[2] = c[2];this->c[3] = c[3];
        this->cs[0] = cs[0];this->cs[1] = cs[1];
        this->cs[2] = cs[2];this->cs[3] = cs[3];
        this->serial=serial;
        }
        Data(){}
        
};


//each buffer element
class Segment
{
    public : 
        Data data;
        bool isACKED;
        bool isBusy;
        long int lastSentTime;

        Segment()
        {
        	char chars[4] = {'-','-','-','-'};
        	char cs[4] = {'-','-','-','-'};
            Data temp(chars,-1,cs);
            data = temp;
            isACKED = false;
            isBusy = false;
            lastSentTime = -1;
        }


        
        

};

class ACK
{
    public : 
        short serial;

        ACK(short serial){this->serial = serial;}

};

class AppData
{
    public : 
        vector<Data> datas;
        int serialCounter = 0;
	
	void CheckExit(string str)
	{
		
		if(str == EXITCALL)
		{
			close(sockfd);
			exit(1);
		
		}
	}




        void LoadString(string str)
        {	
        
        	CheckExit(str);
        
        
        	
        
        	//Getting the input string and packeting and push them into
		//packages to be processed.
		// '\0' indicating the end of the sentence
		// receiving empty spaces of the package also filled with the '\0' sign
		
		

        	int receiver = (str.length()+1)%4;
        	for(int k=0;k<(4-receiver)%4;k++)
        	{
        		str+='\0';
        	}
        	
            for(int i=0;i<str.length();i+=4)
            {
       		
       		char chars[4] = {str[i],str[i+1],str[i+2],str[i+3]};
       		int checksum = str[i] + str[i+1] + str[i+2] + str[i+3];
       		
       		char cs[4] = {Clamp(checksum-381),Clamp(checksum-254)
       				,Clamp(checksum-127),Clamp(checksum)};
       		
       		
       		Data adding(chars,serialCounter++,cs);
                datas.push_back(adding);
       		
                
            }
            
        }
        
        bool HasNullInIt(Data data)
        {
        	if(data.c[0] == 0 || data.c[1] == 0 ||
        	 data.c[2] == 0 || data.c[3] == 0 )return true;
        	 return false;
        }
        

        void PrintIfReceivedFully()
        {
        
        	//Arbitrarily added '\0' indicating the end of a sentence
        	//when facing a '\0', we can print it as it is a full sentence
        	//otherwise dont print yet, wait for the end of the sentence
        	if(!HasNullInIt(datas[datas.size()-1]))return;
        	
        	
        	
        	for(int i=0;i<datas.size();i++)
        	{	
        		for(int j=0;j<4;j++)
        		{
        			if(datas[i].c[j] == 0)break;
        			cout << datas[i].c[j];
        		}
        	}
        	cout << endl;
        	
        	datas.clear();
        }

};

int Abs(int a)
{
    if(a<0) return -a;
    else return a;
}

class ReceiverBuffer
{
    public : 
        AppData appData;
        vector<Segment> window;
        int size;
        short minBufferIndex = 0;
	
        ReceiverBuffer()
        {
            this->size = bufferSize;
            for(int i=0;i<size;i++)
            {
                Segment segment;
                window.push_back(segment);
            }
        }


	//Checking the receiving data,
	//if the checksum holds the actual data
	//process is, otherwise ignore...
	bool CheckCheckSum(Data data)
	{
		int myChecksum = data.c[0]+data.c[1]+data.c[2]+data.c[3];
		int checksum = data.cs[0]+data.cs[1]+data.cs[2]+data.cs[3];
		if(myChecksum == checksum)return true;
		
		
		
		
		return false;
	}


        void DeliverData(Data data)
        {
        	//Reliable, ordered data to be sent to the application
            appData.datas.push_back(data);
            appData.PrintIfReceivedFully();
        }

        void Slide()
        {
        
        	//if minBufferIndex is ACKED, slide the window until encountered with an unACKED 
        	//segment
            int temp = minBufferIndex;
            for(int i=0;i<size;i++)
            {
                if(window[(minBufferIndex+i)%size].isACKED && window[(minBufferIndex+i)%size].isBusy)
                {
                    DeliverData(window[(minBufferIndex+i)%size].data);
                    window[(minBufferIndex+i)%size].isBusy = false;
                    window[(minBufferIndex+i)%size].isACKED = false;
                    temp ++;
                }
                else break;
            }

            temp = temp%(size*2);
            minBufferIndex = temp;

        }

        ACK ReceiveData(Data data)
        {
        	//Checking the checksum, if garbled, return a token with serial -1
        	if(!CheckCheckSum(data))
        	{
        		
        		ACK ack(-1);
        		return ack;
        	}
        
        	//checking the serial of the received data, process and reply, if neccessary.
		ACK ack(data.serial);


            if(data.serial == minBufferIndex)
            {
                window[minBufferIndex%size].data = data;
                window[minBufferIndex%size].isACKED = true;
                window[minBufferIndex%size].isBusy = true;
                Slide();
            }
            else if(minBufferIndex>data.serial && minBufferIndex-data.serial<=size)
            {
                
                return ack;
            }
            else if(minBufferIndex<data.serial && data.serial-minBufferIndex>=size)
            {
                
                return ack;
            }

            else
            {
                int index = (data.serial)%size;
                window[index].data = data;
                window[index].isACKED = true;
                window[index].isBusy = true;
                
            }


                return ack;


        }

        

};

class SenderBuffer
{
    public : 
        AppData appData;
        vector<Segment> window;
        int size;
        int nextDataIndex = 0;
        
        int minBufferIndex = 0;

        SenderBuffer()
        {
            this->size=bufferSize;
            for(int i=0;i<size;i++)
            {
                Segment segment;
                window.push_back(segment);
            }
        }

        Data GetNextData()
        {
        
        	//if NEW data does not exist
            if(appData.datas.size()<=nextDataIndex)
            {
            	char chars[4] = {'-','-','-','-'};
            	char cs[4] = {'-','-','-','-'};
                Data def(chars,-1,cs);
                return def;
            }
            
            //otherwise push new data to the application
            else
            {
                return appData.datas[nextDataIndex];
            }

        }


        void InsertDataToBuffer(Data data,int index)
        {
        	//Get the data and insert it to the buffer, it will be hopefully send...
            window[index].data = data;
            window[index].isACKED = false;
            window[index].isBusy = true;
            window[index].data.serial = window[index].data.serial % (size*2);

            time_t currentTime;
            currentTime = time(NULL);
            currentTime *= 1000;
            window[index].lastSentTime = currentTime;


            nextDataIndex++;
        }

        Data PushDownNextData()
        {
        
        	//if exist data from the user AND exist an available space in the sender buffer
		//return an actual data, otherwise return a temp data with serial -1
            Data data = GetNextData();
            if(data.serial==-1)
            {
                return data;
            }
            
            int index = GetSuitableBufferIndex();
            if(index == -1)
            {
                data.serial = -1;
                return data;
            }
            data.serial = data.serial%(size*2);
            InsertDataToBuffer(data,index);

            return data;

        }

        int GetSuitableBufferIndex()
        {
        
        	//if exist an available space in the sender buffer return the index 
        	//otherwise return -1
            for(int i=0;i<size;i++)
            {
                if(!window[(minBufferIndex+i)%size].isBusy)return (minBufferIndex+i)%size;
            }
            return -1;

        }
        

       

        


        vector<Data> CheckTimeOuts()
        {
        
        	//checking timeouts for each segment, if exist send again and set timeout
        	// as current time
            time_t currentTime;
            currentTime = time(NULL);
            currentTime *= 1000;
            vector<Data> datas;
            for(int i=0;i<size;i++)
            {
                if(!window[i].isACKED && window[i].lastSentTime + timeOut <= currentTime && window[i].isBusy)
                {
                    
                    datas.push_back(window[i].data);
                    window[i].lastSentTime = currentTime;
                }
            }
            return datas;
        }

        void CheckAcks()
        {
        
        	//Updating the window, maybe slided
            int temp = minBufferIndex;
            for(int i=0;i<size;i++)
            {
                if(window[(minBufferIndex+i)%size].isACKED)
                {
                    window[(minBufferIndex+i)%size].isACKED = false;
                    window[(minBufferIndex+i)%size].isBusy = false;
                    temp++;
                }
                else
                {
                    break;
                }
            }
            temp = temp%(size*2);
            minBufferIndex = temp;
        }

        void GetACK(ACK ack)
        {
            //Processing the received ack
            for(int i=0;i<size;i++)
            {
                if(window[i].data.serial == ack.serial)
                {
                    window[i].isACKED = true;
                }
            }
        }
       
};

class DataTransfer
{
    public : 

        SenderBuffer sender;
        ReceiverBuffer receiver;
       

        void SetupPackage(string type,Data data)
        {
            for(int i=0;i<packageLen;i++)package[i] = '-';

		//if the package to send is actual data
            if(type=="data")
            {
                package[0] = 'd';
                package[1] = (char)data.serial;
                package[2] = data.c[0];
                package[3] = data.c[1];
                package[4] = data.c[2];
                package[5] = data.c[3];
                package[6] = data.cs[0];
                package[7] = data.cs[1];
                package[8] = data.cs[2];
                package[9] = data.cs[3];
                
            }
            else //ACK
            {
                package[0] = 'a';
                package[1] = (char)data.serial;
                package[2] = '-';
                package[3] = '-';
                package[4] = '-';
                package[5] = '-';
                package[6] = '-';
                package[7] = '-';
                package[8] = '-';
                package[9] = '-';
            }

        }
        void ResolvePackage()
        {
        	//the first char of the receiving packet indicates the type of data
        	// if it is 'a' means ACK, if it is 'd' means actual data
            if(buffer[0] == 'a')
            {

                ACK ack((short)buffer[1]);
                sender.GetACK(ack);
            }
            else if(buffer[0] == 'd')
            {
		char chars[4] = {buffer[2],buffer[3],buffer[4],buffer[5]};
		char cs[4] = {buffer[6],buffer[7],buffer[8],buffer[9]};
                Data data(chars,(short)buffer[1],cs);
                ACK ack = receiver.ReceiveData(data);
                if(ack.serial == -1) return;
                SendPackage("ack",data);
            }
            

        }
        DataTransfer()
        {
            package = new char[packageLen];
        }
        void UpdateSender()
        {
        	//if there is data in the application and there is an available 
        	// space in the sender buffer, it will get the corresponding data
            Data data = sender.PushDownNextData();
            if(data.serial != -1)
            {
                SendPackage("data",data);
            }

            vector<Data> datas;
            
            
            //checking time outs, if exist, sending again...
            datas = sender.CheckTimeOuts();
            for(int i=0;i<datas.size();i++)
            {
                SendPackage("data",datas[i]);

            }
            //checking tokens, maybe sliding the window
            sender.CheckAcks();

        }
        
        void SendPackage(string type,Data data)
        {
        	SetupPackage(type,data);
        	//setting up the package to send whether it is token or data, its actual data
        	//and other metadata

		sendto(sockfd, (const char *)package, packageLen*sizeof(char), 
		MSG_CONFIRM, (const struct sockaddr *) &cliaddr,
		    len);
	    	
	    	
	    	//next 3 lines for debugging purposes ...
		//cout << "Sent package : " << package[0] << (short)package[1] << package[2]
		//<< package[3] << package[4] << package[5] << (int)package[6] <<
		//(int)package[7] << (int)package[8] << (int)package[9] << endl;
        
        
        }
        

};
DataTransfer chatting;



void GetInput()
{
	while(1)
	{
		//getting input from the user
		string line;
		getline(cin,line);
			
		chatting.sender.appData.LoadString(line);
	}
}

void Receive()
{
	while(1)
	{
	
		n = recvfrom(sockfd, (char *)buffer, 50, 
		        MSG_WAITALL, ( struct sockaddr *) &cliaddr,
		        &len);
	
	
		//next 3 lines for debugging purposes...
		//cout << "Received package : " << buffer[0] << (short)buffer[1] << buffer[2]
		    //<< buffer[3] << buffer[4] << buffer[5] << (int)buffer[6] << (int)buffer[7] <<
		    //(int)buffer[8] << (int)buffer[9] << endl;
	
	
	
		//after receiving the package, sending it to the application to resolve...     
		    chatting.ResolvePackage();
			
		for(int i=0;i<50;i++)buffer[i]='\0';
	}
	
}


void Send()
{
	while(1)
	{
		chatting.UpdateSender();

		
	    	
	    	
	}
	
}

void StartUp()
{
	
          
	    // Creating the socket and bind to it
	    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	    }
	       
	    memset(&servaddr, 0, sizeof(servaddr));
	    memset(&cliaddr, 0, sizeof(cliaddr));
	       
	    // Setting the serverport
	    servaddr.sin_family    = AF_INET; // IPv4
	    servaddr.sin_addr.s_addr = INADDR_ANY;
	    servaddr.sin_port = htons(SERVERPORT);
	       
	    
	    if ( bind(sockfd, (const struct sockaddr *)&servaddr, 
		    sizeof(servaddr)) < 0 )
	    {
		perror("bind failed");
		exit(EXIT_FAILURE);
	    }

	   
	    len = sizeof(cliaddr); 


}

int main(int argc,char **argv) {
    
    //Port number of the server
    SERVERPORT = atoi(argv[1]);
    
    //To make the initialization of the socket and adresses ...
    StartUp();

	
	//Thread 1 : getting standard input from user
	thread t1(GetInput);
	
	//Thread 2 : Controlling the sender buffer throughout the process
	thread t2(Send);
	
	//Thread 3 : Receiving and delivering external data to the application
	thread t3(Receive);
    
    t1.join();
    t2.join();
    t3.join();
    
    
       
    return 0;
}
