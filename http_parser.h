#include <boost/fusion/include/std_pair.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_object.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>

namespace RFC2616{
    enum responses{
        OK = 200,
        NOT_FOUND = 404
    };
}
struct request_line{
    std::string request;
    std::string uri;
    std::string version;
};
struct response_line{
    std::string version;
    ushort code;
    std::string phrase;
};
struct ci_less : std::binary_function<std::string, std::string, bool>
{
    struct nocase_compare : public std::binary_function<unsigned char,unsigned char,bool>
    {
        bool operator() (const unsigned char& c1, const unsigned char& c2) const
        { return tolower (c1) < tolower (c2); }
    };

    bool operator() (const std::string & s1, const std::string & s2) const
    {
        return std::lexicographical_compare
            (s1.begin (), s1.end (),
            s2.begin (), s2.end (),
            nocase_compare ());
    }
};
template<typename T>
class start_line_visitor: public boost::static_visitor<T>
{
    std::function<T(const request_line&)> req_func;
    std::function<T(const response_line&)> resp_func;

public:

    typedef decltype(req_func) RLFType;
    typedef decltype(resp_func) SLFType;

    start_line_visitor() = default;
    start_line_visitor(const RLFType &rlf, const SLFType &slf): req_func(rlf), resp_func(slf){}

    virtual T operator ()(const request_line& ln) const
    {
        if(req_func)
            return req_func(ln);
        return T();
    }

    virtual T operator ()(const response_line& ln) const
    {
        if(resp_func)
            return resp_func(ln);
        return T();
    }
};

class is_request_visitor: public boost::static_visitor<bool>
{
public:
    bool operator ()(const request_line&) const {return true;}
    bool operator ()(const response_line&) const {return false;}
};

class start_line{

public:

    typedef boost::variant<request_line,response_line> line_type;

private:

    bool empty = true;
    line_type line;

public:
    start_line(line_type line) : line(line){empty = false;}
    start_line() = default;
    start_line(const start_line &ln) : line(ln.line), empty(ln.empty) {}
    start_line& operator=(const start_line &ln){this->line = ln.line;this->empty = ln.empty;return *this;}

    std::string toString(){
        static start_line_visitor<std::string> visitor(
                    [](const request_line &l){ return l.request+" "+l.uri+" "+l.version; },
                    [](const response_line &l){ return l.version+" "+boost::lexical_cast<std::string>(l.code)+" "+l.phrase; });
        return line.apply_visitor(visitor);
    }

    void set(const line_type &ln){
        empty = false;
        line = ln;
    }

    bool is_empty() const { return empty; }

    template<class T>
    T get()
    {
        return boost::get<T>(line);
    }

    line_type &get()
    {
        return line;
    }

    void clear() { empty = true; }
    bool is_request(){return boost::apply_visitor(is_request_visitor(),line);}

};

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;

BOOST_FUSION_ADAPT_STRUCT(
    request_line,
    (std::string, request)
    (std::string, uri)
    (std::string, version)
)
template<typename Iterator, typename SpaceType>
struct request_line_parser : public qi::grammar<Iterator,request_line(),SpaceType>{

    request_line_parser() : request_line_parser::base_type(requestLine)
    {
        requestLine %= request >> uri >> version;
        request     %=  qi::lexeme[+(qi::char_("A-Z")-qi::space)];
        uri         %=  qi::lexeme[+(qi::char_-qi::space)];
        version     %=  qi::lexeme[+qi::char_("HTTP/.0-9")];
    }
    qi::rule<Iterator, request_line(), SpaceType> requestLine;
    qi::rule<Iterator, std::string(), SpaceType> request, uri, version;

    start_line operator()(std::string raw){
        request_line line;
        bool ok = qi::phrase_parse(raw.begin(),raw.end(),*this,ascii::blank, line);
        start_line sl;
        sl.set(line);
        return sl;
    }
};
BOOST_FUSION_ADAPT_STRUCT(
    response_line,
    (std::string, version)
    (ushort, code)
    (std::string, phrase)
)
template<typename Iterator, typename SpaceType>
struct response_line_parser : public qi::grammar<Iterator,response_line(),SpaceType>{
    response_line_parser() : response_line_parser::base_type(responseLine){
        responseLine %= version >> code >> phrase;
        phrase %= qi::lexeme[+(qi::char_("A-Z")-qi::space)];
        code %= qi::ushort_;
        version   %=  qi::lexeme[+qi::char_("HTTP/.0-9")];
    }
    qi::rule<Iterator, response_line(), SpaceType> responseLine;
    qi::rule<Iterator, std::string(), SpaceType> phrase, version;
    qi::rule<Iterator, ushort(), SpaceType> code;
    start_line operator()(std::string raw){
        response_line line;
        bool ok = qi::phrase_parse(raw.begin(),raw.end(),*this,ascii::blank, line);
        start_line sl;
        if(ok)
            sl.set(line);
        return sl;
    }
};

class start_line_parser {

public:
    start_line operator()(std::string raw){
        request_line_parser<std::string::iterator,ascii::blank_type>req_parser;
        start_line line = req_parser(raw);
        if(line.is_empty()){
            request_line_parser<std::string::iterator,ascii::blank_type>res_parser;
            line = res_parser(raw);
        }
        return line;
    }
};
typedef std::map<std::string,std::string, ci_less> string_map;
struct http_field{

    std::string value;
    string_map params;
    bool empty;

    bool operator ==(const http_field &obj) const
    {
        if(value!=obj.value) return false;
        if(params!=obj.params) return false;
        return true;
    }

    bool operator !=(const http_field &obj) const
    {
        if(value!=obj.value) return true;
        if(params!=obj.params) return true;
        return false;
    }

    http_field() : empty(true){}

    void setValue(const std::string &val)
    {
        empty = false;
        value = val;
    }
    std::string getValue()const
    {
        return value;
    }
    void setParam(const std::string &name,const std::string& val)
    {
        params[name] = val;
    }
    std::string getParam(std::string name)
    {
        if(params.count(name))
            return params.at(name);
        return std::string();
    }
};
BOOST_FUSION_ADAPT_STRUCT(
    http_field,
    (std::string, value)
    (string_map, params)
)
typedef std::map<std::string,http_field,ci_less> body_type;
struct http_raw_packet{
    std::string start;
    body_type body;
    std::string content;

    void operator = (const http_raw_packet &other)
    {
        start = other.start;
        body = other.body;
        content = other.content;
    }
};
BOOST_FUSION_ADAPT_STRUCT(
    http_raw_packet,
    (std::string, start)
    (body_type, body)
    (std::string,content)
)

template <class Iterator,class SpaceType>
struct http_parser_rule : public qi::grammar<Iterator,http_raw_packet(),SpaceType, qi::locals<char>>{


    qi::rule<Iterator, http_raw_packet(), SpaceType, qi::locals<char>>http;
    qi::rule<Iterator, std::string(), SpaceType, qi::locals<char>>start;
    qi::rule<Iterator, body_type(), SpaceType, qi::locals<char>>query;
    qi::rule<Iterator, std::string(), SpaceType, qi::locals<char>>content;
    qi::rule<Iterator, std::pair<std::string, http_field>(), SpaceType, qi::locals<char>>pair;
    qi::rule<Iterator, http_field(), SpaceType, qi::locals<char>>rfield;
    qi::rule<Iterator, std::string(), SpaceType, qi::locals<char>>value, p_key, p_value;
    qi::rule<Iterator, std::pair<std::string, std::string>(), SpaceType, qi::locals<char>>param;
    qi::rule<Iterator, std::string(), SpaceType, qi::locals<char>>name;

      http_parser_rule() : http_parser_rule::base_type(http)
        {
            http    %= start >> qi::eol >> query >> qi::eol >> *((qi::eol) >> content);
            start   %= qi::lexeme[+(qi::char_-qi::eol)];
            query   %= pair >> *(qi::eol >> pair);
            content %= qi::lexeme[+(qi::char_)];
            pair    %= name >> ':' >> rfield;
            name    %= qi::lexeme[+(qi::char_-qi::eol-qi::char_(':'))];
            rfield  %= value >> *((qi::lit(';')) >> param);
            value   %= qi::lexeme[+(qi::char_-qi::char_(';')-qi::eol)];
            param   %= p_key >> '=' >> p_value;
            p_key   %= qi::lexeme[+(qi::char_-qi::char_('='))];
            p_value %= qi::lexeme[+(qi::char_-qi::char_(';')-qi::eol)];
        }

};

class http_parser{

    http_parser_rule<std::string::iterator, ascii::blank_type>raw_parse;
    bool _error = true;

public:


    http_parser() = default;
    std::string form(const http_raw_packet &pack)
    {
        auto param =  [](const string_map &sm)
        {
            std::string str = sm.empty() ? "" : ";";
            for(auto it : sm)
            {
                str.append(it.first);
                str.append("=");
                str.append(it.second);
                str.append(",");
            }
            if(str.size())str.pop_back();
            return str;
        };
        std::string array(pack.start);
        array.append("\r\n");
        for(auto it: pack.body)
        {
            array.append(it.first);
            array.append(": ");
            array.append(it.second.value);
            array.append(param(it.second.params));
            array.append("\r\n");
        }

        if(!pack.content.empty())
        {
            array.append("\r\n");
            array.append(pack.content);
        }

        return array;
    }
    http_raw_packet parse(std::string raw)
    {
        http_raw_packet pack;
        _error = qi::phrase_parse(raw.begin(),raw.end(),raw_parse, ascii::blank,pack);
        return pack;
    }
    bool error()const
    {
        return _error;
    }
};
class http_packet{

    http_raw_packet *origin;

public:

    http_packet(http_raw_packet *pack)
    {
        origin = pack;
    }
    start_line get_start()const
    {
        start_line_parser p;
        return p(origin->start);
    }
    std::string get_content()const
    {
        return origin->content;
    }
    http_raw_packet get_origin()const
    {
        return *origin;
    }
    http_field operator[](std::string name)const
    {
        if(origin->body.count(name))
            return origin->body.at(name);
        return http_field();
    }
    void set_field(std::string name,http_field field)
    {
        origin->body[name] = field;
    }
    void set_start(start_line line)
    {
        origin->start = line.toString();
    }
    void set_content(std::string content)
    {
        origin->content = content;
    }
    bool is_request()
    {
        start_line_parser p;
       return p(origin->start).is_request();
    }
};