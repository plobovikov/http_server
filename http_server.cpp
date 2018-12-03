#include <iostream>
#include <functional>
#include <map>
#include <queue>
#include <fstream>

#include "posix_thread_wrapper.h"
#include "http_parser.h"

using namespace std;

class http_server{

    std::queue<int>clients;
    std::vector<m_thread::thread*>threads;
    m_thread::mutex mtx;
    m_thread::condition_variable cond_var;
    int sock;

    static http_raw_packet read_from_socket(int cs)
    {
        char buffer[2048+1];
        http_parser parser;

        int size = read(cs, buffer, 2048);

        std::cerr << "Received packet:\n";
        buffer[size] = '\0';
        std::cerr << buffer << std::endl;

        auto pack = parser.parse(std::string(buffer,strlen(buffer)));
        return pack;
    }
    static std::string load_from_file(ifstream &file)
    {
        std::string result;
        char buffer[2000];
        bool flag;
        do
        {
            flag = static_cast<bool>(file.read(buffer,2000));
            result.append(std::string(buffer,file.gcount()));
        }
        while(flag);
        file.close();
        return result;
    }
    static void write_to_socket(int sock,http_raw_packet response)
    {
        http_parser p;
        write(sock,p.form(response).c_str(),p.form(response).size());
        std::cerr << "Send packet:\n" << p.form(response);
    }

    static http_raw_packet generate_response(RFC2616::responses response)
    {
        http_raw_packet resp;
        static std::map<RFC2616::responses,std::string>response_map{
            std::make_pair(RFC2616::OK,"OK"),
            std::make_pair(RFC2616::NOT_FOUND,"Not Found")
        };
        resp.start = "HTTP/1.1 " + boost::lexical_cast<std::string>(response) + " " + response_map[response];
        return resp;
    }

public:

    http_server(int port,int poll_size) : mtx(m_thread::mutex::Normal){
        struct sockaddr_in ss_addr;

        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(sock == -1)perror("Error creating socket");

        setsockopt(sock, 1, SO_REUSEADDR, 0, 0);

        ss_addr.sin_family = AF_INET;
        ss_addr.sin_addr.s_addr = INADDR_ANY;
        ss_addr.sin_port = htons(port);

        if(bind(sock, (struct sockaddr *) &ss_addr, sizeof(ss_addr)) != 0){perror("Error binding socket\n");};
        if (listen(sock, 10) != 0)perror("Error listen socket");

        for(int i(0);i != poll_size;++i)threads.push_back(new m_thread::thread(m_thread::thread::Detached,&http_server::thread_handle,&clients,&mtx,&cond_var));
    }
    void start(){
        for(;;) {

            struct sockaddr_in cs_addr;
            socklen_t cs_len = sizeof(cs_addr);

            int cs = accept(sock, (struct sockaddr *) &cs_addr, &cs_len);
            if (cs != -1){mtx.lock();clients.push(cs);cond_var.notify_one();mtx.unlock();}

        }
    }
    ~http_server(){close(sock);for(auto it : threads)delete it;}

private:

    static void thread_handle(std::queue<int> *clients,m_thread::mutex *mtx, m_thread::condition_variable *cond_var)
    {
        for(;;){

            int sock;
            mtx->lock();
            while(!clients->size()){
                cond_var->wait(*mtx);
            }
            sock = clients->back();
            clients->pop();
            mtx->unlock();

            auto pack = read_from_socket(sock);
            http_packet packet(&pack);
            auto line = packet.get_start().get<request_line>();
            if(line.request.compare("GET") == 0)handle_get(packet,sock);
            close(sock);
        }

    }
    static void handle_get(http_packet pack,int sock)
    {
        static std::map<std::string,std::string>format_map{
            std::make_pair("html","text/html"),
                    std::make_pair("gif","image/gif"),
                    std::make_pair("png","image/png"),
                    std::make_pair("jpg","image/jpeg"),
                    std::make_pair("css","text/css"),
                    std::make_pair("js","application/javascript"),
                    std::make_pair("swf","application/x-shockwave-flash"),
                    std::make_pair("ico","image/x-icon"),
                    std::make_pair("","text/plain"),
                    std::make_pair("txt","text/plain"),
                    std::make_pair("php","application/x-php")
        };

        http_raw_packet response;
        std::ifstream file;
        std::vector<std::string>segments;
        http_field field1,field2;

        std::string file_name = pack.get_start().get<request_line>().uri;
        boost::replace_all(file_name,"%20","_");

        file.open("/home/paul/http/my_dir" + file_name,std::ios::in | std::ios::binary);

        if(!file.is_open()){
            write_to_socket(sock,generate_response(RFC2616::NOT_FOUND));
            return;
        }

        boost::algorithm::split(segments,file_name,boost::is_any_of("."));

        response = generate_response(RFC2616::OK);
        auto info = load_from_file(file);

        field1.value = format_map[segments.back()];
        field2.value = boost::lexical_cast<std::string>(info.size());
        field1.params.insert(std::make_pair("charset","utf-8"));
        response.body.insert(std::make_pair("Content-Type",field1));
        response.body.insert(std::make_pair("Content-Length",field2));
        response.content = info;

        write_to_socket(sock,response);
    }
};
int main()
{
    http_server server(1026,8);
    server.start();
    return 0;
}

