#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
// used namespace
using namespace std;
// Structure for options
typedef struct Options
{
    char* hostname;
    string file_name;
    int port;
    bool download;
} options;
/*
*   Used constants and variables
*/
const int REQ_ARGC = 7;
const int REQ_COMB = 20;
const size_t RESP_BUFF = 40;
const size_t MAX_BUFF_SIZE = 1024;
/*
* Prototypes of functions
*/
void err_print(const char *msg);
bool args_err(int argc, char** argv, options* args);
bool connect_to(char* hostname, int port, bool download, string file_name);
bool start_upload(int socket, string target, size_t size);
bool start_download(int socket, string target, size_t size);
string generate_message(bool download,string file_name, size_t* size);
/*
*   Main
*/
int main(int argc, char **argv)
{   
    options args;
    if ( args_err(argc,argv,&args) ) 
    {
        perror("Wrong parameters, usage: ./client -h hostname -p port [-­d|u] file_name");
        return EXIT_FAILURE;
    }

    if ( connect_to(args.hostname, args.port, args.download, args.file_name) ) 
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
/*
*   For checking arguments
*/
bool args_err(int argc, char** argv, options* args)
{
    if (argc != REQ_ARGC)
    {
        return EXIT_FAILURE;
    }
    int opt,port,h=0,p=0,u=0,d=0;
    char* garbage = '\0';
    while ((opt = getopt(argc,argv,"h:p:u:d:")) != EOF)
    {
        switch(opt)
        {
            case 'h': 
            {
                h += 10; args->hostname = optarg ; break;
            }
            case 'u':
            { 
                u++; args->file_name = optarg; args->download=false; break;
            }
            case 'd': 
            {
                d++; args->file_name = optarg; args->download=true; break;
            }
            case 'p': 
            {
                port = strtoul(optarg,&garbage,0);
                if ((garbage[0]!='\0') || (port < 1024) || (port > 65535)) 
                {
                    return EXIT_FAILURE;
                }
                p += 9; args->port = port; 
                break;
            }
            case '?': 
            {
                return EXIT_FAILURE;
            }
        }
    }
    if ((h > 10) || (p > 9) || (u > 1) || (d > 1) || ((h+p+d+u) != REQ_COMB))
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
/*
*   For connection handling
*/
bool connect_to(char* hostname, int port, bool download, string file_name)
{   
    int sockfd, code;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char resp_buffer[RESP_BUFF];
    size_t up = 0;
    string req_msg = generate_message(download,file_name, &up);
    if (req_msg == "ERR")
    {
        return EXIT_FAILURE;
    }
    /* Create a socket point */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
    {
        perror("ERROR opening socket");
        return EXIT_FAILURE;
    }
    /* Maybe use for */
    server = gethostbyname(hostname);
    if (server == NULL) 
    {
        fprintf(stderr,"ERROR, no such host\n");
        return EXIT_FAILURE;
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(port);
    /* Now connect to the server */
    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
    {
        perror("ERROR connecting");
        return EXIT_FAILURE;
    }
    /* Send message to the server */
    code = write(sockfd, req_msg.c_str(), req_msg.size());
    if (code < 0) 
    {
        perror("ERROR writing to socket");
        return EXIT_FAILURE;
    }
    /* Now read server response */
    bzero(resp_buffer,RESP_BUFF);
    code = read(sockfd, resp_buffer, RESP_BUFF-1);
    if (code < 0) 
    {
        perror("ERROR reading from socket");
        return EXIT_FAILURE;
    }
    string resp = static_cast<string>(resp_buffer);
    size_t size = resp.find("\r\n");
    string act = resp.substr(0,size);
    resp.erase(0,size+2);
    if (act == "OK")
    {
        size = resp.find("\r\n");
        size = atoi(resp.substr(0,size).c_str());
    }
    // deciding what to do
    if (download && (act == "OK"))
    {
        return start_download(sockfd, file_name, size);
    }
    else if (!download && (act == "READY"))
    {
        return start_upload(sockfd, file_name, up);        
    }
    else
    {
        cerr << (resp_buffer) << endl;
        return EXIT_FAILURE;
    }
}
/*
*   For creating initial message to server
*/
string generate_message(bool download,string file_name, size_t* size)
{
    string req_msg = "";
    if (download)
    {
        req_msg += "SEND\r\n";
        req_msg += file_name+"\r\n";
    }
    else
    {
        req_msg += "SAVE\r\n";
        req_msg += file_name + "\r\n";
        struct stat filestatus;
        if (stat( file_name.c_str(), &filestatus ) != 0)
        {
            perror("ERROR opening local file to upload");
            return "ERR";
        }
        stringstream ss;
        *size = filestatus.st_size;
        ss << filestatus.st_size;
        req_msg += ss.str() + "\r\n";
    }
    return req_msg;
}
/*
*   Upload to server handling
*/
bool start_upload(int socket, string target, size_t size)
{
    size_t bytes_sent = 0;
    ifstream file;
    file.open (target, ios::in );  
    if (! file.is_open())
    {
        perror("ERROR can not open file");
        return EXIT_FAILURE;
    }

    char data [MAX_BUFF_SIZE];
    bzero(data, MAX_BUFF_SIZE);
    while(size != bytes_sent)
    {
        file.read(data,MAX_BUFF_SIZE);
        bytes_sent += file.gcount();
        if ((write(socket,data,file.gcount())) < 0) 
        {
            perror("ERROR writing to socket");
            return EXIT_FAILURE;
        }

        bzero(data, MAX_BUFF_SIZE);
    }
    return EXIT_SUCCESS;
}
/*
*   Download from server handling
*/
bool start_download(int socket, string target, size_t size)
{
    ofstream file;
    stringstream ss;
    ss << socket;
    string tmp = target+"_("+ ss.str() +").temporary";
    file.open (tmp, ios::out | ios::binary );

    if (! file.is_open())
    {
        perror("ERR can not create file");
        return EXIT_FAILURE;
    }

    if ((write(socket,"READY\r\n",10)) < 0)
    {
        perror("ERROR writing to socket");
        return EXIT_FAILURE;
    }

    int res;
    size_t total=0;
    char part[MAX_BUFF_SIZE];
    while ((res = read(socket, part, MAX_BUFF_SIZE)))
    {
        total += res;
        if (res < 1) 
        {
            perror("ERROR while getting response");
            return EXIT_FAILURE;
        }
        else 
        { 
            file.write(part,res);
            bzero(part, MAX_BUFF_SIZE); // buff erase
        }
    }
    file.close(); 
    if ( total != size)
    {
        if (remove( tmp.c_str() ) != 0) perror("ERROR deleting temporary file");
        perror("ERROR size of temporary file do not match");
        return EXIT_FAILURE;
    }
       
    if (rename( tmp.c_str() , target.c_str() ) != 0)
    {
        perror("ERROR while renamig temp file");
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
