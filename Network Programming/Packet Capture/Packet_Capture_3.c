#include<stdio.h>	//For standard things
#include<stdlib.h>	//malloc
#include<string.h>	//memset
#include<sys/types.h>
#include<sys/socket.h>
#include<net/if.h>
#include<netinet/in.h>
#include<netinet/if_ether.h>
#include<netinet/tcp.h> //Provides declarations for tcp header
#include<netinet/udp.h> //Provides declarations for udp header
#include<netinet/in_systm.h>
#include<netinet/ip.h> //Provides declarations for ip header
#include<netinet/ip_icmp.h> //Provides declarations for icmp header
#include<arpa/inet.h>

void ProcessPacket(unsigned char* , int);
void print_ip_header(unsigned char* , int);
void print_packet(unsigned char* , int);
void PrintData (unsigned char* , int);

int sock_raw;
FILE *logfile;
int tcp=0,udp=0,icmp=0,others=0,igmp=0,total=0,i,j;
struct sockaddr_in source,dest;

int main()
{
	int saddr_size , data_size;
	struct sockaddr saddr;
	struct in_addr in;
	
	unsigned char *buffer = (unsigned char *)malloc(65536); //Its Big!
	
	logfile=fopen("log.txt","w");
	if(logfile==NULL) printf("Unable to create file.");
	printf("Starting...\n");
	//Create a raw socket that shall sniff
	sock_raw = socket(AF_INET , SOCK_RAW , IPPROTO_TCP);
	if(sock_raw < 0)
	{
		printf("Socket Error\n");
		return 1;
	}
	while(1)
	{
		saddr_size = sizeof saddr;
		//Receive a packet
		data_size = recvfrom(sock_raw , buffer , 65536 , 0 , &saddr , &saddr_size);
		if(data_size <0 )
		{
			printf("Recvfrom error , failed to get packets\n");
			return 1;
		}
		//Now process the packet
		ProcessPacket(buffer , data_size);
	}
	close(sock_raw);
	printf("Finished");
	return 0;
}

void ProcessPacket(unsigned char* buffer, int size)
{
	//Get the IP Header part of this packet
	struct ip *iph = (struct ip*)buffer;
	++total;
	switch (iph->ip_p) //Check the Protocol and do accordingly...
	{
		case 1:  //ICMP Protocol
			++icmp;
			break;
		
		case 2:  //IGMP Protocol
			++igmp;
			break;
		
		case 6:  //TCP Protocol
			++tcp;
			print_packet(buffer , size);
			break;
		
		case 17: //UDP Protocol
			++udp;
			print_packet(buffer , size);
			break;
		
		default: //Some Other Protocol like ARP etc.
			++others;
			break;
	}
	printf("TCP : %d   UDP : %d   ICMP : %d   IGMP : %d   Others : %d   Total : %d\r",tcp,udp,icmp,igmp,others,total);
}

void print_ip_header(unsigned char* Buffer, int Size)
{
	unsigned short iphdrlen;
		
	struct ip *iph = (struct ip *)Buffer;
	iphdrlen =iph->ip_hl*4;
	
	memset(&source, 0, sizeof(source));
	source.sin_addr.s_addr = iph->ip_src;
	
	memset(&dest, 0, sizeof(dest));
	dest.sin_addr.s_addr = iph->ip_dst;
	
	fprintf(logfile,"\n");
	fprintf(logfile,"IP Header\n");
	fprintf(logfile,"   |-IP Version        : %d\n",(unsigned int)iph->ip_v);
	fprintf(logfile,"   |-IP Header Length  : %d DWORDS or %d Bytes\n",(unsigned int)iph->ip_hl,((unsigned int)(iph->ip_hl))*4);
	fprintf(logfile,"   |-Type Of Service   : %d\n",(unsigned int)iph->ip_tos);
	fprintf(logfile,"   |-IP Total Length   : %d  Bytes(Size of Packet)\n",ntohs(iph->ip_len));
	fprintf(logfile,"   |-Identification    : %d\n",ntohs(iph->ip_id));
	fprintf(logfile,"   |-Fragment OFF      : %d\n",ntohs(iph->ip_off));
	fprintf(logfile,"   |-TTL      : %d\n",(unsigned int)iph->ip_ttl);
	fprintf(logfile,"   |-Protocol : %d\n",(unsigned int)iph->ip_p);
	fprintf(logfile,"   |-Checksum : %d\n",ntohs(iph->ip_sum));
	fprintf(logfile,"   |-Source IP        : %s\n",inet_ntoa(source.sin_addr));
	fprintf(logfile,"   |-Destination IP   : %s\n",inet_ntoa(dest.sin_addr));
}

void print_packet(unsigned char* Buffer, int Size)
{
	unsigned short iphdrlen;
	
	struct ip *iph = (struct ip *)Buffer;
	iphdrlen = iph->ip_hl*4;
	
			
	fprintf(logfile,"\n\n***********************TCP Packet*************************\n");	
		
	print_ip_header(Buffer,Size);
	fprintf(logfile,"\n");
	fprintf(logfile,"                        DATA Dump                         ");
	fprintf(logfile,"\n");
		
	fprintf(logfile,"IP Header\n");
	PrintData(Buffer,iphdrlen);
		
	fprintf(logfile,"Data Payload\n");	
	PrintData(Buffer + iphdrlen , (Size - iph->ip_hl*4) );
						
	fprintf(logfile,"\n###########################################################");
}

void PrintData (unsigned char* data , int Size)
{
	
	for(i=0 ; i < Size ; i++)
	{
		if( i!=0 && i%16==0)   //if one line of hex printing is complete...
		{
			fprintf(logfile,"         ");
			for(j=i-16 ; j<i ; j++)
			{
				if(data[j]>=32 && data[j]<=128)
					fprintf(logfile,"%c",(unsigned char)data[j]); //if its a number or alphabet
				
				else fprintf(logfile,"."); //otherwise print a dot
			}
			fprintf(logfile,"\n");
		} 
		
		if(i%16==0) fprintf(logfile,"   ");
			fprintf(logfile," %02X",(unsigned int)data[i]);
				
		if( i==Size-1)  //print the last spaces
		{
			for(j=0;j<15-i%16;j++) fprintf(logfile,"   "); //extra spaces
			
			fprintf(logfile,"         ");
			
			for(j=i-i%16 ; j<=i ; j++)
			{
				if(data[j]>=32 && data[j]<=128) fprintf(logfile,"%c",(unsigned char)data[j]);
				else fprintf(logfile,".");
			}
			fprintf(logfile,"\n");
		}
	}
}
