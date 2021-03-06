#include<Rcpp.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netdb.h>

//using namespace Rcpp;

inline bool fequal(float a, float b)
{
  const float epsilon = 0.0001;
  return ( fabs(a - b) < epsilon );
}


inline bool should_send(float sample_rate)
{
  if ( fequal(sample_rate, 1.0) )
  {
    return true;
  }
  
  float p = ((float)random() / RAND_MAX);
  return sample_rate > p;
}

struct _StatsdClientData {
  int     sock;
  struct  sockaddr_in server;
  
  std::string  ns;
  std::string  host;
  short   port;
  bool    init;
  
  char    errmsg[1024];
};

class Statsd {
public:
  Statsd(const std::string& host="127.0.0.1", int port=8125, const std::string& ns = "");
  void config(const std::string& host, int port, const std::string& ns = "");
  int inc(const std::string& key, float sample_rate = 1.0);
  int dec(const std::string& key, float sample_rate = 1.0);
  int count(const std::string& key, size_t value, float sample_rate = 1.0);
  int gauge(const std::string& key, size_t value, float sample_rate = 1.0);
  int timing(const std::string& key, size_t ms, float sample_rate = 1.0);
  ~Statsd();
  void print();
  
private:
  int init();
  int send(const std::string& message);
  int send(std::string key, size_t value,
           const std::string& type, float sample_rate);
  void cleanup(std::string& key);
  int send_to_daemon(const std::string &message);
    
protected:
  struct _StatsdClientData* d;
};

//constructors
Statsd::Statsd(const std::string& host,
                           int port,
                           const std::string& ns) {
  d = new _StatsdClientData;
  d->sock = -1;
  config(host, port, ns);
}

void Statsd::config(const std::string& host, int port, const std::string& ns)
{
  d->ns = ns;
  d->host = host;
  d->port = port;
  d->init = false;
  if ( d->sock >= 0 ) {
    close(d->sock);
  }
  d->sock = -1;
}

int Statsd::init()
{
  if ( d->init ) return 0;
  
  d->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if ( d->sock == -1 ) {
    snprintf(d->errmsg, sizeof(d->errmsg), "could not create socket, err=%m");
    return -1;
  }
  
  memset(&d->server, 0, sizeof(d->server));
  d->server.sin_family = AF_INET;
  d->server.sin_port = htons(d->port);
  
  int ret = inet_aton(d->host.c_str(), &d->server.sin_addr);
  if ( ret == 0 )
  {
    // host must be a domain, get it from internet
    struct addrinfo hints, *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    
    ret = getaddrinfo(d->host.c_str(), NULL, &hints, &result);
    if ( ret ) {
      close(d->sock);
      d->sock = -1;
      snprintf(d->errmsg, sizeof(d->errmsg),
               "getaddrinfo fail, error=%d, msg=%s", ret, gai_strerror(ret) );
      return -2;
    }
    struct sockaddr_in* host_addr = (struct sockaddr_in*)result->ai_addr;
    memcpy(&d->server.sin_addr, &host_addr->sin_addr, sizeof(struct in_addr));
    freeaddrinfo(result);
  }
  
  d->init = true;
  return 0;
}

int Statsd::dec(const std::string& key, float sample_rate)
{
  return count(key, -1, sample_rate);
}

int Statsd::inc(const std::string& key, float sample_rate)
{
  return count(key, 1, sample_rate);
}

int Statsd::gauge(const std::string& key, size_t value, float sample_rate)
{
  return send(key, value, "g", sample_rate);
}

int Statsd::timing(const std::string& key, size_t ms, float sample_rate)
{
  return send(key, ms, "ms", sample_rate);
}

int Statsd::count(const std::string& key, size_t value, float sample_rate)
{
  return send(key, value, "c", sample_rate);
}


int Statsd::send(std::string key, size_t value, const std::string &type, float sample_rate)
{
  if (!should_send(sample_rate)) {
    return 0;
  }
  
  cleanup(key);
  
  char buf[256];
  if ( fequal( sample_rate, 1.0 ) )
  {
    snprintf(buf, sizeof(buf), "%s%s:%zd|%s",
             d->ns.c_str(), key.c_str(), value, type.c_str());
  }
  else
  {
    snprintf(buf, sizeof(buf), "%s%s:%zd|%s|@%.2f",
             d->ns.c_str(), key.c_str(), value, type.c_str(), sample_rate);
  }
  
  return send(buf);
}

int Statsd::send(const std::string &message) 
{
    return send_to_daemon(message);
}

void Statsd::cleanup(std::string& key)
{
  size_t pos = key.find_first_of(":|@");
  while ( pos != std::string::npos )
  {
    key[pos] = '_';
    pos = key.find_first_of(":|@");
  }
}

int Statsd::send_to_daemon(const std::string &message) {
  //std::cout << "send_to_daemon: " << message.length() << " B" << std::endl;
  int ret = init();
  if ( ret )
  {
    return ret;
  }
  ret = sendto(d->sock, message.data(), message.size(), 0, (struct sockaddr *) &d->server, sizeof(d->server));
  if ( ret == -1) {
    snprintf(d->errmsg, sizeof(d->errmsg),
             "sendto server fail, host=%s:%d, err=%m", d->host.c_str(), d->port);
    return -1;
  }
  
  return 0;
}

Statsd::~Statsd()
{
}

//print function
void Statsd::print(){
  Rcpp::Rcout << "x = " << "test" << std::endl;
}

RCPP_MODULE(statsdmodule){
  Rcpp::class_<Statsd>( "Statsd" )
  .constructor<std::string,int, std::string>("initialize a new statsD client")
  .method( "config", &Statsd::config, "allow to change the configuration during runtime")
  .method( "inc", &Statsd::inc, "inc increments a statsd count type")
  .method( "dec", &Statsd::dec, "dec decrements a statsd count type")
  .method( "count", &Statsd::count, "count submits a stats count type")
  .method( "gauge", &Statsd::gauge, "gauge submits/updates a statsd gauge type")
  .method( "timing", &Statsd::timing, "timing submits a statsd timing type")
  ;
}
