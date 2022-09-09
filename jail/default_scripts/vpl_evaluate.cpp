/**
 * VPL builtin program for submissions evaluation
 * @Copyright (C) 2019 Juan Carlos Rodríguez-del-Pino
 * @License http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
 * @Author Juan Carlos Rodríguez-del-Pino <jcrodriguez@dis.ulpgc.es>
 */

#include <cstdlib>
#include <cstdio>
#include <climits>
#include <limits>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <pty.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <cmath>
#include <execinfo.h>
#include <regex.h>
#include <string>
#include <algorithm>

//other required headers
#include <fstream>
#include <exception>
#include <variant>
#include <unordered_map>
#include <memory>
#include <map>
#include <set>


using namespace std;

const int MAXCOMMENTS = 20;
const int MAXCOMMENTSLENGTH = 100*1024;
const int MAXCOMMENTSTITLELENGTH = 1024;
const int MAXOUTPUT = 256* 1024 ;//256Kb


////////////////////////
//Json Parser/////////// note: this parser is too complex, latter should try to implement a simpler one
namespace json {
  /* Declarations */

  struct jsonWrapper;

  enum json_t{Null_t=0, Bool_t, Number_t, String_t, Array_t, Object_t};

  //json types
  using Null   = std::nullptr_t;
  using Bool   = bool;
  using Number = long double;
  using String = std::string;
  using Array  = std::vector<std::shared_ptr<jsonWrapper>>;
  using Object = std::unordered_map<String, std::shared_ptr<jsonWrapper>>;
  using Value  = std::variant<Null, Bool, Number, String, Array, Object>;


  struct jsonWrapper{
    //data
    std::shared_ptr<Value> data;
    
    //set
    inline void
    set(Value const& v);

    inline void
    operator =(jsonWrapper const& j);

    inline void
    operator =(Value const v);

    //constructors
    jsonWrapper()
      {set(Null());}
    jsonWrapper(std::shared_ptr<Value> const d)
      {data = d;}
    jsonWrapper(Value const& v)
      {set(v);}
    jsonWrapper(std::string const& filepath)
      {parseFile(filepath);}
    jsonWrapper(std::string::const_iterator begin, std::string::const_iterator const end)
      {parse(begin, end);}

    //parse
    void inline
    parse(std::string::const_iterator& c, std::string::const_iterator const end);

    void
    parseFile(std::string const& filepath);

    //get
    inline json_t
    type() const;

    template<typename T>
    inline T&
    get() const;

    jsonWrapper
    at(size_t const n) const;

    jsonWrapper
    at(String const& k) const;

    jsonWrapper&
    operator [](size_t const n);

    jsonWrapper&
    operator [](String const& k);

    jsonWrapper&
    operator ()(String const& k);

    bool
    find(std::string const& k) const;

    //add
    void
    emplace(size_t const n, json::Value const v);

    void
    emplace(std::string const& k, json::Value const v);

    void
    emplaceFile(size_t const n, std::string const& filepath);

    void
    emplaceFile(std::string const& k, std::string const& filepath);

    //destroy
    void
    erase(size_t n);

    void
    erase(std::string const& k);

    ~jsonWrapper() = default;

    //throw
    inline void
    throwIfItsNotAnObject() const;

    inline void
    throwIfItsNotAnArray() const;
  };

  //parse functions declaration
  namespace parse{
    Null
    parseNull(std::string::const_iterator& c, std::string::const_iterator const end); //not used
    Bool
    parseBool(std::string::const_iterator& c, std::string::const_iterator const end); //not used
    Number
    parseNumber(std::string::const_iterator& c, std::string::const_iterator const end);
    String
    parseString(std::string::const_iterator& c, std::string::const_iterator const end);
    Array
    parseArray(std::string::const_iterator& c, std::string::const_iterator const end);
    Object
    parseObject(std::string::const_iterator& c, std::string::const_iterator const end);
    Value
    parseValue(std::string::const_iterator& c, std::string::const_iterator const end);
    Value
    parseFile(std::string const& filepath);
  }


  /* definitions */

  //parse exception
  class ParseError: public std::exception{
    public:
    ParseError(): msg("unknow"){}
    ParseError(std::string const str): msg(str){};

    virtual const char* what() const throw(){
      return msg.c_str();
    }

    private:
    std::string msg;
  };

  //useful functions
  namespace tools {
    bool
    strMatch(std::string const& sub, std::string::const_iterator& c, std::string::const_iterator const end){
      std::string::const_iterator spc = c;
      std::string::const_iterator sbc = sub.begin();
      bool res   = true;

      while ((sbc!=sub.end()) && (spc!=end)){
        if (*sbc != *spc) {res = false; break;}
        ++spc, ++sbc;
      }
      if (res) c = spc;
      return res;
    }

    bool inline
    isWhitespace(char const c){
      return (std::string(" \n\t\r").find(c) != std::string::npos);
    }

    char
    parseEscapeSequence(std::string::const_iterator& c, std::string::const_iterator const end){
      char res;
      std::size_t new_n = 0;
      auto shexToChar = [&c, &new_n, end](){return (char) std::stoi(std::string(c, (c+4<end)?c+4:end), &new_n, 16);};

      switch (*c)
      {
      case '\"':  {res = '\"'; c++; break;}
      case '\\':  {res = '\\'; c++; break;}
      case '/':   {res = '/' ; c++; break;}
      case 'b':   {res = '\b'; c++; break;}
      case 'f':   {res = '\f'; c++; break;}
      case 'n':   {res = '\n'; c++; break;}
      case 'r':   {res = '\r'; c++; break;}
      case 't':   {res = '\t'; c++; break;}
      case 'u':   {c++; res = shexToChar(); c += new_n; break;}
      default:    {throw ParseError("expected escape sequence");}
      }

      return res;
    }

    bool
    gettext(std::string const& filepath, std::string& str){
      std::ifstream       file(filepath, std::ifstream::in);
      std::stringstream   text;
      std::string         line;
      std::string         errmsg;

      if(!file.is_open()) return false;
      while(std::getline(file, line)) text << line;
      file.close();

      str = text.str();
      return true;
    }

    std::string
    json_tToStr(size_t t){
      std::vector<std::string> s {"Null", "Bool", "Number", "String", "Array", "Object"};
      return (t!=std::variant_npos? "json::" + s.at(t): "__empty_notype__");
    }
  }

  //jsonWrapper methods
  void inline
  jsonWrapper::parse(std::string::const_iterator& c, std::string::const_iterator const end){
    data = std::make_shared<Value>(parse::parseValue(c, end));
  }

  void
  jsonWrapper::parseFile(std::string const& filepath){
    data = std::make_shared<Value>(parse::parseFile(filepath));
  }

  template<typename T>
  inline T&
  jsonWrapper::get() const{
    return std::get<T>(*data);
  }

  inline json_t
  jsonWrapper::type() const{
    return json_t(data->index());
  }

  jsonWrapper
  jsonWrapper::at(size_t const n) const{
    throwIfItsNotAnArray();
    return (*(get<Array>().at(n)));
  }

  jsonWrapper
  jsonWrapper::at(String const& k) const{
    throwIfItsNotAnObject();
    return (*(get<Object>().at(k)));
  }

  jsonWrapper&
  jsonWrapper::operator [](size_t const n){
    throwIfItsNotAnArray();
    return (*(get<Array>()[n]));
  }

  jsonWrapper&
  jsonWrapper::operator [](String const& k){
    throwIfItsNotAnObject();
    //if('k' is not found) emplace 'k' as json::Null
    if (!find(k))
      get<Object>().emplace(k, std::make_shared<jsonWrapper>());
    return (*(get<Object>()[k]));
  }

  jsonWrapper&
  jsonWrapper::operator ()(String const& k){
    //if('k' is not found) emplace 'k' as json::Object
    get<Object>().emplace(k, std::make_shared<jsonWrapper>(Object()));
    return *(get<Object>()[k]);
  }

  bool
  jsonWrapper::find(std::string const& k) const{
    throwIfItsNotAnObject();
    return (get<Object>().find(k) != get<Object>().end());
  }
  
  inline void
  jsonWrapper::set(Value const& v){
    data = std::make_shared<Value>(v);
  }

  inline void
  jsonWrapper::operator =(jsonWrapper const& j){
    data = j.data;
  }

  inline void
  jsonWrapper::operator =(Value const v){
    data = std::make_shared<Value>(v);
  }

  void
  jsonWrapper::emplace(size_t const n, json::Value const v){
    throwIfItsNotAnArray();
    get<Array>().emplace(get<Array>().begin()+n, std::make_shared<jsonWrapper>(std::make_shared<Value>(v)));
  }

  void
  jsonWrapper::emplace(std::string const& k, json::Value const v){
    throwIfItsNotAnObject();
    get<Object>().emplace(k, std::make_shared<jsonWrapper>(std::make_shared<Value>(v)));
  }

  void
	jsonWrapper::emplaceFile(size_t const n, std::string const& filepath){
    throwIfItsNotAnArray();
    emplace(n, parse::parseFile(filepath));
  }

	void
	jsonWrapper::emplaceFile(std::string const& k, std::string const& filepath){
    throwIfItsNotAnObject();
    emplace(k, parse::parseFile(filepath));
  }

  void
  jsonWrapper::erase(size_t n){
    throwIfItsNotAnArray();
    get<Array>().erase(get<Array>().begin()+n);
  }

  void
  jsonWrapper::erase(std::string const& k){
    throwIfItsNotAnObject();
    get<Object>().erase(k);
  }

  inline void
  jsonWrapper::throwIfItsNotAnObject() const{
    if(type() != Object_t)
      throw std::out_of_range("not an Object: type()=="+tools::json_tToStr(data->index())+'\n');
  }

  inline void
  jsonWrapper::throwIfItsNotAnArray() const{
    if(type() != Array_t)
      throw std::out_of_range("not an Array: type()=="+tools::json_tToStr(data->index())+'\n');
  }

  //parse functions
  namespace parse{
    Null
    parseNull(std::string::const_iterator& c, std::string::const_iterator const end){
      if (tools::strMatch("null", c, end)) {return Null{};}
      else throw ParseError("expected Null type: 'null'");
    }

    Bool
    parseBool(std::string::const_iterator& c, std::string::const_iterator const end){
      if (tools::strMatch("false", c, end)) {return Bool{false};}
      else
      if (tools::strMatch("true", c, end))  {return Bool{true};}
      else throw ParseError("expected Bool type: 'true' or 'false'");
    }

    Number
    parseNumber(std::string::const_iterator& c, std::string::const_iterator const end){
      std::size_t new_n = 0;
      Number res = 0;
      try{
        res = std::stod(std::string(c, end), &new_n);
        c += new_n;
      }
      catch(std::invalid_argument()){
        throw ParseError("expected Number type");
      }

      return res;
    }

    String
    parseString(std::string::const_iterator& c, std::string::const_iterator const end){
        std::string res;
        bool str_ended  = false;

        if (*c=='\"') c++;
        while( c != end && !str_ended ){
          switch (*c)
          {
          case '\\':  {c++; res += tools::parseEscapeSequence(c, end); break;}
          case '\"':  {str_ended = true; c++; break;}
          default:    {res += *c; c++; break;}
          }
        }
        if (!str_ended)
            throw ParseError("expected '\"'");

        return res;
    }

    Array
    parseArray(std::string::const_iterator& c, std::string::const_iterator const end){
      Array res;
      bool arr_ended    = false;
      bool expect_value = true;
      bool got_value    = false;
      if (*c=='[') c++;

      while( c != end && !arr_ended ){
        if (tools:: isWhitespace(*c))
            {c++; continue;}
        switch (*c){
        case ',': {
            if(!got_value)
                throw ParseError("expected value");
            got_value       = false;
            expect_value    = true;
            c++;
            break;
        }
        case ']': {
            if(!got_value && !res.size())
                throw ParseError("expected Value");
            arr_ended       = true;
            c++;
            break;
        }
        default:  {
            if(!expect_value && got_value)
                throw ParseError("expected ',' or ']'");
            got_value       = true;
            expect_value    = false;
            std::shared_ptr<jsonWrapper> new_wrapper = std::make_shared<jsonWrapper>(std::make_shared<Value>(parseValue(c, end)));
            res.push_back(new_wrapper);
            break;
        }
        }
      }

      if (!arr_ended) throw ParseError("expected ']'");
      return res;
    }

    Object
    parseObject(std::string::const_iterator& c, std::string::const_iterator const end){
      Object res;
      bool obj_ended    = false;
      bool expect_value = true;
      bool got_value    = false;

      if (*c=='{') c++;

      while( c != end && !obj_ended ){
        if (tools:: isWhitespace(*c))
            {c++; continue;}

        switch (*c){
        case ',': {
            if(!got_value)
                throw ParseError("expected value");
            got_value       = false;
            expect_value    = true;
            c++;
            break;
        }
        case '}': {
            if(!got_value && !res.size())
                throw ParseError("expected Value");
            obj_ended       = true;
            c++;
            break;
        }
        default:  {
            if(!expect_value && got_value)
                throw ParseError("expected ',' or '}'");
            got_value       = true;
            expect_value    = false;
            std::string name = parseString(c, end);
            while (tools::isWhitespace(*c) && c!=end)
                {c++; continue;}
            if(*c!=':' || c==end)
                throw ParseError("expected ':'");
            c++;
            res[name] = std::make_shared<jsonWrapper>(std::make_shared<Value>(parseValue(c, end)));
            break;
        }
        }
      }

      if (!obj_ended) throw ParseError("expected '}'");
      return res;
    }

    Value
    parseValue(std::string::const_iterator& c, std::string::const_iterator const end){
      Value value;
      if (c == end) return {};
      while(c != end) {
        if (tools::isWhitespace(*c))                {c++; continue;}
        else if (*c == '{')                         return parseObject(c, end);
        else if (*c == '\"')                        return parseString(c, end);
        else if (*c == '[')                         return parseArray(c, end);
        else if (*c == '-' || std::isdigit(*c))     return parseNumber(c, end);
        else if (*c == 'f'){
          if(tools::strMatch("false", c, end))
            return Bool{false};
          else throw ParseError("invalid input");
        }
        else if (*c == 't'){
          if(tools::strMatch("true", c, end))
            return Bool{true};
          else throw ParseError("invalid input");
        }
        else if (*c == 'n'){
          if(tools::strMatch("null", c, end))
            return Null{};
          else throw ParseError("invalid input");
        }
        else throw ParseError("invalid input");
      }

      return value;
    }

    Value
    parseFile(std::string const& filepath){
      std::string text;
      if(!tools::gettext(filepath, text))
        throw ParseError('"'+filepath+"\" could not be opened.");
      std::string::const_iterator c = text.begin();
      return parseValue(c, text.end());
    }
  }
}

///////////////////////////
// End of Json Parser//////

//extra tools
std::map<string, string> lang_map ={
  {"pt_PT.UTF-8", "br"},
  {"en_US.UTF-8", "en"}
};

std::map<string, string> ext_map ={
  {"ada", "ada"},
  {"adb", "ada"},
  {"ads", "ada"},
  {"all", "all"},
  {"asm", "asm"},
  {"c", "c"},
  {"cc", "cpp"},
  {"cpp", "cpp"},
  {"C", "cpp"},
  {"c++", "cpp"},
  {"clj", "clojure"},
  {"cs", "csharp"},
  {"d", "d"},
  {"erl", "erlang"},
  {"go", "go"},
  {"groovy", "groovy"},
  {"java", "java"},
  {"js", "javascript"},
  {"scala", "scala"},
  {"sql", "sql"},
  {"scm", "scheme"},
  {"s", "mips"},
  {"kt", "kotlin"},
  {"lisp", "lisp"},
  {"lsp", "lisp"},
  {"lua", "lua"},
  {"sh", "shell"},
  {"pas", "pascal"},
  {"p", "pascal"},
  {"f77", "fortran"},
  {"f90", "fortran"},
  {"f", "fortran"},
  {"for", "fortran"},
  {"pl", "prolog"},
  {"pro", "prolog"},
  {"htm", "html"},
  {"html", "html"},
  {"hs", "haskell"},
  {"m", "matlab"},
  {"mzn", "minizinc"},
  {"perl", "perl"},
  {"prl", "perl"},
  {"php", "php"},
  {"py", "python"},
  {"v", "verilog"},
  {"vh", "verilog"},
  {"vhd", "verilog"},
  {"vhdl", "verilog"},
  {"r", "r"},
  {"R", "r"},
  {"rb", "ruby"},
  {"ruby", "ruby"},
  {"ts", "typescript"}
};

string inline
getFileExtension(string const& file){
  return string(file.begin() + file.rfind('.') +1, file.end());
}

template<typename T>
vector<T> inline
setToVector(set<T> const& s){
  return vector<T>(s.begin(), s.end());
}

namespace tokentools{
  vector<string>
  strsplt(string const& str, string const& delim){
    string split;
    vector<string> v;
    size_t i=0, c=0;
    while(i!=std::string::npos){
      i = str.find(delim, c);
      split = str.substr(c, (i==std::string::npos?i:i-c));
      v.emplace_back(split);
      c = i+delim.size();
    }
    return v;
  }

  std::vector<std::string>
  gettkns(std::string const& str, std::string const& form, std::string const& tkn="&$var"){
    std::vector<std::string> const splt = strsplt(form, tkn);
    std::vector<std::string> tkns;
    size_t c1=0, s1=0;
    for(size_t i=0; i<splt.size(); i++){
      c1 = str.find(splt[i], c1+s1)+splt[i].size();
      s1 = (i+1<splt.size()? str.find(splt[i+1], c1) - c1: std::string::npos);
      tkns.emplace_back(str.substr(c1,s1));
    }
    return tkns;
  }

  std::string
  puttkns(std::string const& form, std::vector<std::string> const& tkns, std::string const& tkn="&$var"){
    std::vector<std::string> const splt = strsplt(form, tkn);
    std::string res;
    for(size_t i=0; i<splt.size(); i++){
      res += splt[i];
      if(i<tkns.size()) res += tkns[i];
    }
    return res;
  }
}

bool may_enhance=false;


//translate/enhance interface
struct Interface{
  /* data */
	string const
  dirEvaluate
    = "./lang/evaluate/";

  string const
  dirEnhance
    = "./lang/enhance/";

	string
  _lang;

  vector<string>
  _files;

	json::jsonWrapper
  evaluate;
  json::jsonWrapper
  enhance;

  bool _loaded_evaluate=false, _loaded_enhance=false;

  /* methods */
	Interface(vector<string> const& files){
    _files = files;
    _lang = "en";
    evaluate.set(json::Value(json::Object()));
    enhance.set(json::Value(json::Object()));
  }

	Interface(vector<string> const& files, string lang){
    _files = files;
    _lang = lang;
    evaluate.set(json::Value(json::Object()));
    enhance.set(json::Value(json::Object()));
  }

	bool
  loadTransLangLib(){
    try{
      evaluate.emplaceFile("en", dirEvaluate + "en.json");
      string const filename = dirEvaluate + _lang + ".json";
      std::ifstream file(filename);
      if (file.is_open()){
        string line, text;
        while(std::getline(file, line)) text += line;
        file.close();
        std::string::const_iterator c(text.begin());
        evaluate(_lang) = json::parse::parseValue(c, text.end());
      }
      else{
        evaluate(_lang) = evaluate["en"];
      }
      _loaded_evaluate = true;
    }
    catch (json::ParseError &e){
      fprintf(stdout, "json: ParseError: %s\n", e.what());
      _loaded_evaluate = false;
    }
    catch (std::out_of_range &e){
      fprintf(stdout, "json: out_of_range: %s\n", e.what());
      _loaded_evaluate = false;
    }

		return _loaded_evaluate;
	}

  bool
  loadEnhacedLangLib(){
    try{
      for(string& _file : _files){
        enhance(_file).emplaceFile("default", dirEnhance + _file + '/' + "default.json");
        string const 	filename = dirEnhance + _file + '/' + _lang + ".json";
        std::ifstream file(filename);
        if (file.is_open()){
          string line, text;
          while(std::getline(file, line)) text += line;
          file.close();
          std::string::const_iterator c(text.begin());
          enhance(_file)(_lang) = json::parse::parseValue(c, text.end());
          continue;
        }
        enhance(_file)(_lang) = enhance[_file]["default"];
      }
      _loaded_enhance = true;
    }
    catch (json::ParseError &e){
      fprintf(stdout, "json parser error: %s\n", e.what());
      _loaded_enhance = false;
    }
    catch (std::out_of_range &e){
      fprintf(stdout, "json acess error: %s\n", e.what());
      _loaded_enhance = false;
    }

    return _loaded_enhance;
  }

  string
  langEvaluate(int const id){
    if(evaluate.find(_lang)){
      if(evaluate.at(_lang).find(to_string(id)))
        return evaluate.at(_lang).at(to_string(id)).get<json::String>();
    }
    else if(evaluate.find("en")){
      if(evaluate.at("en").find(to_string(id)))
        return evaluate.at("en").at(to_string(id)).get<json::String>();
    }
    throw std::out_of_range("langEvaluate error: missing 'id'");
  }

  std::pair<string, std::vector<std::string>>
  _getridtkns(string const& info, string const& file){
    json::Object const df = enhance[file]["default"].get<json::Object>();
    for (auto const& [id, _data] : df){
      auto data = _data->get<json::String>();
      if (data.find("&$var")!=string::npos){
        vector<string> v = tokentools::strsplt(data, "&$var");
        bool match = true;
        for(string const& s: v){
          if(info.find(s)==string::npos){
            vector<string>().swap(v);
            match = false;
            break;
          }
        }
        if (match){
          vector<string> rt = tokentools::gettkns(info, data);
          return {id, rt};
        }
      }
      else if(info == data) return {id, vector<string>()};
    }
    return {"", vector<string>()};
  }

  // WIP
  string
  enhanceMessageDiv(string const& info, string const& file=""){
    string const fl = (file==""? _files.at(0): file);
    auto [id, tkn] = _getridtkns(info, fl);
    if(id=="")
      return "<case>" + info;
    if(tkn.size()==0)
      return "<caseEnhanced>" + enhance[fl][_lang][id].get<json::String>() + "<caseOriginal>" + info;
    return "<caseEnhanced>"
    + tokentools::puttkns(
      enhance[fl][_lang][id].get<json::String>(),
      tkn
      )
    + "<caseOriginal>"
    + info;
  }

  string
  enhanceMessage(string const& info, string const& file=""){
    string const fl = (file==""? _files.at(0): file);
    auto [id, tkn] = _getridtkns(info, fl);
    if(id=="")
      return info;
    if(tkn.size()==0)
      return enhance[fl][_lang][id].get<json::String>();
    return
      tokentools::puttkns(
      enhance[fl][_lang][id].get<json::String>(),
      tkn
      );
  }
};

Interface* L;


/**
 * Class Tools Declaration
 */
class Tools {
public:
	static bool existFile(string name);
	static string readFile(string name);
	static vector<string> splitLines(const string &data);
	static int nextLine(const string &data);
	static string caseFormat(string text, bool enhance/*=false||may_enhance*/);
	static string toLower(const string &text);
	static string normalizeTag(const string &text);
	static bool parseLine(const string &text, string &name, string &data);
	static string trimRight(const string &text);
	static string trim(const string &text);
	static void fdblock(int fd, bool set);
	static bool convert2(const string& str, double &data);
	static bool convert2(const string& str, long int &data);
	static const char* getenv(const char* name, const char* defaultvalue);
	static double getenv(const char* name, double defaultvalue);
};

/**
 * Class Stop Declaration
 */
class Stop{
	static volatile bool TERMRequested;
public:
	static void setTERMRequested();
	static bool isTERMRequested();
};

/**
 * Class Timer Declaration
 */
class Timer{
	static time_t startTime;
public:
	static void start();
	static int elapsedTime();
};

/**
 * Class I18n Declaration
 */
class I18n{
public:
	void init();
	const char *get_string(const char *s);
};

/**
 * Interface OutputChecker
 */
class OutputChecker{
protected:
	string text;

public:
	OutputChecker(const string &t):text(t){}
	virtual ~OutputChecker(){};
	virtual string type(){return "";}
	virtual operator string (){return "";}
	virtual string outputExpected(){return text;}
	virtual string studentOutputExpected(){return text;}
	virtual bool match(const string&)=0;
	virtual OutputChecker* clone()=0;
};

/**
 * Class NumbersOutput Declaration
 */
class NumbersOutput:public OutputChecker{
	struct Number{
		bool isInteger;
		long int integer;
		double cientific;

		bool set(const string& str);
		bool operator==(const Number &o)const;
		bool operator!=(const Number &o)const;
		operator string () const;
	};

	vector<Number> numbers;
	bool startWithAsterisk;
	string cleanText;

	static bool isNum(char c);
	static bool isNumStart(char c);
	bool calcStartWithAsterisk();

public:
	NumbersOutput(const string &text);//:OutputChecker(text);
	string studentOutputExpected();
	bool operator==(const NumbersOutput& o)const;
	bool match(const string& output);
	OutputChecker* clone();
	static bool typeMatch(const string& text);
	string type();
	operator string () const;
};

/**
 * Class TextOutput Declaration
 */
class TextOutput:public OutputChecker{
	vector<string> tokens;
	bool isAlpha(char c);

public:
	TextOutput(const string &text);//:OutputChecker(text);
	bool operator==(const TextOutput& o);
	bool match(const string& output);
	OutputChecker* clone();
	static bool typeMatch(const string& text);
	string type();
};

/**
 * Class ExactTextOutput Declaration
 */
class ExactTextOutput:public OutputChecker{
	string cleanText;
	bool startWithAsterix;
	bool isAlpha(char c);

public:
	ExactTextOutput(const string &text);//:OutputChecker(text);
	string studentOutputExpected();
	bool operator==(const ExactTextOutput& o);
	bool match(const string& output);
	OutputChecker* clone();
	static bool typeMatch(const string& text);
	string type();
};

/**
 * Class RegularExpressionOutput Declaration
 * Regular Expressions implemented by:
 * Daniel José Ojeda Loisel
 * Juan David Vega Rodríguez
 * Miguel Ángel Viera González
 */
class RegularExpressionOutput:public OutputChecker {
	string errorCase;
	string cleanText;
	regex_t expression;
	bool flagI;
	bool flagM;
	int reti;

public:
	RegularExpressionOutput (const string &text, const string &actualCaseDescription);

	bool match (const string& output);
		// Regular Expression compilation (with flags in mind) and comparison with the input and output evaluation

	string studentOutputExpected();
		// Returns the expression without flags nor '/'

	OutputChecker* clone();

	static bool typeMatch(const string& text);
		// Tests if it's a regular expression. A regular expressions should be between /../

	string type();
};
/**
 * Class Case Declaration
 * Case represents cases
 */
class Case {
	string input;
	vector< string > output;
	string caseDescription;
	float gradeReduction;
	string failMessage;
	string programToRun;
	string programArgs;
	int expectedExitCode; // Default value std::numeric_limits<int>::min()
	string variation;
public:
	Case();
	void reset();
	void addInput(string );
	string getInput();
	void addOutput(string );
	const vector< string > & getOutput();
	void setFailMessage(const string &);
	string getFailMessage();
	void setCaseDescription(const string &);
	string getCaseDescription();
	void setGradeReduction(float);
	float getGradeReduction();
	void setExpectedExitCode(int);
	int getExpectedExitCode();
	void setProgramToRun(const string &);
	string getProgramToRun();
	void setProgramArgs(const string &);
	string getProgramArgs();
	void setVariation(const string &);
	string getVariation();
};

/**
 * Class TestCase Declaration
 * TestCase represents cases to tested
 */
class TestCase {
	const char *command;
	const char **argv;
	static const char **envv;
	int id;
	bool correctOutput;
	bool outputTooLarge;
	bool programTimeout;
	bool executionError;
	bool correctExitCode;
	char executionErrorReason[1000];
	int sizeReaded;
	string input;
	vector< OutputChecker* > output;
	string caseDescription;
	float gradeReduction;
	float gradeReductionApplied;
	string failMessage;
	string programToRun;
	string programArgs;
	string variantion;
	int expectedExitCode; // Default value std::numeric_limits<int>::min()
	int exitCode; // Default value std::numeric_limits<int>::min()
	string programOutputBefore, programOutputAfter, programInput;

	void cutOutputTooLarge(string &output);
	void readWrite(int fdread, int fdwrite);
	void addOutput(const string &o, const string &actualCaseDescription);
public:
	static void setEnvironment(const char **environment);
	void setDefaultCommand();
	TestCase(const TestCase &o);
	TestCase& operator=(const TestCase &o);
	~TestCase();
	TestCase(int id, const string &input, const vector<string> &output,
			const string &caseDescription, const float gradeReduction,
		    string failMessage, string programToRun, string programArgs, int expectedExitCode);
	bool isCorrectResult();
	bool isExitCodeTested();
	float getGradeReduction();
	void setGradeReductionApplied(float r);
	float getGradeReductionApplied();
	string getCaseDescription();
	string getCommentTitle(bool withGradeReduction/*=false*/); // Suui
	string getComment();
	void splitArgs(string);
	void runTest(time_t timeout);
	bool match(string data);
};

/**
 * Class Evaluation Declaration
 */
class Evaluation {
	int maxtime;
	float grademin, grademax;
	string variation;
	bool noGrade;
	float grade;
	int nerrors, nruns;
	vector<TestCase> testCases;
	char comments[MAXCOMMENTS + 1][MAXCOMMENTSLENGTH + 1];
	char titles[MAXCOMMENTS + 1][MAXCOMMENTSTITLELENGTH + 1];
	char titlesGR[MAXCOMMENTS + 1][MAXCOMMENTSTITLELENGTH + 1];
	volatile int ncomments;
	volatile bool stopping;
	static Evaluation *singlenton;
	Evaluation();

public:
	static Evaluation* getSinglenton();
	static void deleteSinglenton();
	void addTestCase(Case &);
	void removeLastNL(string &s);
	bool cutToEndTag(string &value, const string &endTag);
	void loadTestCases(string fname);
	bool loadParams();
	void addFatalError(const char *m);
	void runTests();
	void outputEvaluation();
};

/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////// END OF DECLARATIONS ///////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// BEGINNING OF DEFINITIONS ////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////

volatile bool Stop::TERMRequested = false;
time_t Timer::startTime;
const char **TestCase::envv=NULL;
Evaluation* Evaluation::singlenton = NULL;

/**
 * Class Tools Definitions
 */

bool Tools::existFile(string name) {
	FILE *f = fopen(name.c_str(), "r");
	if (f != NULL) {
		fclose(f);
		return true;
	}
	return false;
}

string Tools::readFile(string name) {
	char buf[1000];
	string res;
	FILE *f = fopen(name.c_str(), "r");
	if (f != NULL)
		while (fgets(buf, 1000, f) != NULL)
			res += buf;
	return res;
}

vector<string> Tools::splitLines(const string &data) {
	vector<string> lines;
	int len, l = data.size();
	int startLine = 0;
	char pc = 0, c;
	for (int i = 0; i < l; i++) {
		c = data[i];
		if (c == '\n') {
			len = i - startLine;
			if (pc == '\r')
				len--;
			lines.push_back(data.substr(startLine, len));
			startLine = i + 1;
		}
		pc = c;
	}
	if (startLine < l) {
		len = l - startLine;
		if (pc == '\r')
			len--;
		lines.push_back(data.substr(startLine, len));
	}
	return lines;
}

int Tools::nextLine(const string &data) {
	int l = data.size();
	for (int i = 0; i < l; i++) {
		if (data[i] == '\n')
			return i + 1;
	}
	return l;
}

string Tools::caseFormat(string text, bool enhance=false) {
	vector<string> lines = Tools::splitLines(text);
	string res;
	int nlines = lines.size();
	for (int i = 0; i < nlines; i++)
		res += (enhance||may_enhance ? L->enhanceMessage(lines[i]) : lines[i]) + '\n';
	return res;
}

bool Tools::parseLine(const string &text, string &name, string &data) {
	size_t poseq;
	if ((poseq = text.find('=')) != string::npos) {
		name = normalizeTag(text.substr(0, poseq + 1));
		data = text.substr(poseq + 1);
		return true;
	}
	name = "";
	data = text;
	return false;
}

string Tools::toLower(const string &text) {
	string res = text;
	int len = res.size();
	for (int i = 0; i < len; i++)
		res[i] = tolower(res[i]);
	return res;
}

string Tools::normalizeTag(const string &text) {
	string res;
	int len = text.size();
	for (int i = 0; i < len; i++) {
		char c = text[i];
		if (isalpha(c) || c == '=')
			res += tolower(c);
	}
	return res;
}

string Tools::trimRight(const string &text) {
	int len = text.size();
	int end = -1;
	for (int i = len - 1; i >= 0; i--) {
		if (!isspace(text[i])) {
			end = i;
			break;
		}
	}
	return text.substr(0, end + 1);
}

string Tools::trim(const string &text) {
	int len = text.size();
	int begin = len;
	int end = -1;
	for (int i = 0; i < len; i++) {
		char c = text[i];
		if (!isspace(c)) {
			begin = i;
			break;
		}
	}
	for (int i = len - 1; i >= 0; i--) {
		char c = text[i];
		if (!isspace(c)) {
			end = i;
			break;
		}
	}
	if (begin <= end)
		return text.substr(begin, (end - begin) + 1);
	return "";
}

void Tools::fdblock(int fd, bool set) {
	int flags;
	if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
		return;
	}
	if (set && (flags | O_NONBLOCK) == flags)
		flags ^= O_NONBLOCK;
	else
		flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

bool Tools::convert2(const string& str, double &data){
	if ( str == "." ){
		return false;
	}
	stringstream conv(str);
	conv >> data;
	return conv.eof();
}

bool Tools::convert2(const string& str, long int &data){
	stringstream conv(str);
	conv >> data;
	return conv.eof();
}
const char* Tools::getenv(const char* name, const char* defaultvalue) {
	const char* value = ::getenv(name);
	if ( value == NULL ) {
		value = defaultvalue;
	    printf((L->langEvaluate(1)).c_str(), defaultvalue, name);
	}
	return value; // Fixes bug found by Peter Svec
}

double Tools::getenv(const char* name, double defaultvalue) {
	const char* svalue = ::getenv(name);
	double value = defaultvalue;
	if ( svalue != NULL ) {
		Tools::convert2(svalue, value);
	} else {
		printf((L->langEvaluate(1)).c_str(), defaultvalue, name);
	}
	return value;
}


/**
 * Class Stop Definitions
 */

void Stop::setTERMRequested() {
	TERMRequested = true;
}

bool Stop::isTERMRequested() {
	return TERMRequested;
}

/**
 * Class Timer Definitions
 */

void Timer::start() {
	startTime = time(NULL);
}

int Timer::elapsedTime() {
	return time(NULL) - startTime;
}

/**
 * Class Stop Definitions
 */

void I18n::init(){

}

const char *I18n::get_string(const char *s){
	return s;
}

/**
 * Class NumbersOutput Definitions
 */

// Struct Number
bool NumbersOutput::Number::set(const string& str){
	isInteger=Tools::convert2(str, integer);
	if(!isInteger){
		return Tools::convert2(str, cientific);
	}
	return true;
}

bool NumbersOutput::Number::operator==(const Number &o)const{
	if(isInteger)
		return o.isInteger && integer == o.integer;
	if(o.isInteger)
		return cientific != 0?fabs((cientific - o.integer) / cientific) < 0.0001 : o.integer == 0;
	else
		return cientific != 0?fabs((cientific - o.cientific) / cientific) < 0.0001 : fabs(o.cientific) < 0.0001;
}

bool NumbersOutput::Number::operator!=(const Number &o)const{
	return !((*this)==o);
}

NumbersOutput::Number::operator string() const{
	char buf[100];
	if(isInteger) {
		sprintf(buf, "%ld", integer);
	} else {
		sprintf(buf, "%10.5lf", cientific);
	}
	return buf;
}


bool NumbersOutput::isNum(char c){
	if(isdigit(c)) return true;
	return c=='+' || c=='-' || c=='.' || c=='e' || c=='E';
}

bool NumbersOutput::isNumStart(char c){
	if(isdigit(c)) return true;
	return c=='+' || c=='-' || c=='.';
}

bool NumbersOutput::calcStartWithAsterisk(){
	int l=text.size();
	for(int i=0; i<l; i++){
		char c=text[i];
		if(isspace(c)) continue;
		if(c=='*'){
			cleanText = text.substr(i+1,text.size()-(i+1));
			return true;
		}else{
			cleanText = text.substr(i,text.size()-i);
			return false;
		}
	}
	return false;
}

NumbersOutput::NumbersOutput(const string &text):OutputChecker(text){
	int l=text.size();
	string str;
	Number number;
	for(int i=0; i<l; i++){
		char c=text[i];
		if((isNum(c) && str.size()>0) || (isNumStart(c) && str.size()==0)){
			str+=c;
		}else if(str.size()>0){
			if(isNumStart(str[0]) && number.set(str)) numbers.push_back(number);
			str="";
		}
	}
	if(str.size()>0){
		if(isNumStart(str[0]) && number.set(str)) numbers.push_back(number);
	}
	startWithAsterisk=calcStartWithAsterisk();
}

string NumbersOutput::studentOutputExpected(){
	return cleanText;
}

bool NumbersOutput::operator==(const NumbersOutput& o)const{
	size_t l=numbers.size();
	if( o.numbers.size() < l ) return false;
	int offset = 0;
	if(startWithAsterisk)
		offset = o.numbers.size()-l;
	for(size_t i = 0; i < l; i++)
		if(numbers[i] != o.numbers[offset+i])
			return false;
	return true;
}

bool NumbersOutput::match(const string& output){
	NumbersOutput temp(output);
	return operator==(temp);
}

OutputChecker* NumbersOutput::clone(){
	return new NumbersOutput(outputExpected());
}

bool NumbersOutput::typeMatch(const string& text){
	int l=text.size();
	string str;
	Number number;
	for(int i=0; i<l; i++){
		char c=text[i];
		// Skip spaces/CR/LF... and *
		if(!isspace(c) && c!='*') {
			str += c;
		}else if(str.size()>0) {
			if (!isNumStart(str[0])||
				!number.set(str)) return false;
			str="";
		}
	}
	if(str.size()>0){
		if(!isNumStart(str[0])||!number.set(str)) return false;
	}
	return true;
}

string NumbersOutput::type(){
	return (L->langEvaluate(3)).c_str();
}

NumbersOutput::operator string () const{
	string ret="[";
	int l=numbers.size();
	for(int i=0; i<l; i++){
		ret += i > 0 ? ", " : "";
		ret += numbers[i];
	}
	ret += "]";
	return ret;
}

/**
 * Class TextOutput Definitions
 */

bool TextOutput::isAlpha(char c){
	if ( isalnum(c) ) return true;
	return c < 0;
}

TextOutput::TextOutput(const string &text):OutputChecker(text){
	size_t l = text.size();
	string token;
	for(size_t i = 0; i < l; i++){
		char c = text[i];
		if( isAlpha(c) ){
			token += c;
		}else if(token.size() > 0){
			tokens.push_back(Tools::toLower(token));
			token="";
		}
	}
	if(token.size()>0){
		tokens.push_back(Tools::toLower(token));
	}
}

bool TextOutput::operator==(const TextOutput& o) {
	size_t l = tokens.size();
	if (o.tokens.size() < l) return false;
	int offset = o.tokens.size() - l;
	for (size_t i = 0; i < l; i++)
		if (tokens[i] != o.tokens[ offset + i ])
			return false;
	return true;
}

bool TextOutput::match(const string& output) {
	TextOutput temp(output);
	return operator== (temp);
}

OutputChecker* TextOutput::clone() {
	return new TextOutput(outputExpected());
}

bool TextOutput::typeMatch(const string& text) {
	return true;
}

string TextOutput::type(){
	return (L->langEvaluate(4)).c_str();
}

/**
 * Class ExactTextOutput Definitions
 */

bool ExactTextOutput::isAlpha(char c){
	if(isalnum(c)) return true;
	return c < 0;
}

ExactTextOutput::ExactTextOutput(const string &text):OutputChecker(text){
	string clean = Tools::trim(text);
	if(clean.size() > 2 && clean[0] == '*') {
		startWithAsterix = true;
		cleanText = clean.substr(2, clean.size() - 3);
	}else{
		startWithAsterix =false;
		cleanText=clean.substr(1,clean.size()-2);
	}
}

string ExactTextOutput::studentOutputExpected(){
	return cleanText;
}

bool ExactTextOutput::operator==(const ExactTextOutput& o){
	return match(o.text);
}

bool ExactTextOutput::match(const string& output){
	if (cleanText == output) return true;
	string cleanOutput = output;
	// Removes last output char if is a newline and the last searched char is not a newline.
	if (cleanText.size() > 0 && cleanText[cleanText.size()-1] != '\n' ) {
		if (cleanOutput.size() > 0 && cleanOutput[cleanOutput.size()-1] == '\n' ) {
			cleanOutput = cleanOutput.substr(0, cleanOutput.size()-1);
		}
	}
	if (startWithAsterix && cleanText.size() < cleanOutput.size()) {
		size_t start = cleanOutput.size() - cleanText.size();
		return cleanText == cleanOutput.substr(start, cleanText.size());
	} else {
		return cleanText == cleanOutput;
	}
}

OutputChecker* ExactTextOutput::clone(){
	return new ExactTextOutput(outputExpected());
}

bool ExactTextOutput::typeMatch(const string& text){
	string clean=Tools::trim(text);
	return (clean.size()>1 && clean[0]=='"' && clean[clean.size()-1]=='"')
			||(clean.size()>3 && clean[0]=='*' && clean[1]=='"' && clean[clean.size()-1]=='"');
}

string ExactTextOutput::type(){
	return (L->langEvaluate(5)).c_str();
}

/**
 * Class RegularExpressionOutput Definitions
 */

RegularExpressionOutput::RegularExpressionOutput(const string &text, const string &actualCaseDescription):OutputChecker(text) {
	errorCase = actualCaseDescription;
	size_t pos = 1;
	flagI = false;
	flagM = false;
	string clean = Tools::trim(text);
	pos = clean.size() - 1;
	while (clean[pos] != '/' && pos > 0) {
		pos--;
	}
	cleanText = clean.substr(1,pos-1);
	if (pos + 1 != clean.size()) {
		pos = pos + 1;
		// Flags processing
		while (pos < clean.size()) {
			switch (clean[pos]) {
				case 'i':
					flagI=true;
					break;
				case 'm':
					flagM=true;
					break;
				case ' ':
					break;
				default:
					Evaluation* p_ErrorTest = Evaluation::getSinglenton();
					char wrongFlag = clean[pos];
					string flagCatch;
					stringstream ss;
					ss << wrongFlag;
					ss >> flagCatch;
					string errorType = string((L->langEvaluate(39)).c_str())+ string(errorCase)+ string ((L->langEvaluate(40)).c_str()) + string(flagCatch) + string ((L->langEvaluate(41)).c_str());
					const char* flagError = errorType.c_str();
					p_ErrorTest->addFatalError(flagError);
					p_ErrorTest->outputEvaluation();
					abort();
			}
			pos++;
		}
	}
}

// Regular Expression compilation (with flags in mind) and comparison with the input and output evaluation
bool RegularExpressionOutput::match (const string& output) {

	reti=-1;
	const char * in = cleanText.c_str();
	// Use POSIX-C regrex.h
	// Flag compilation
	if (flagI || flagM) {
		if (flagM && flagI) {
			reti = regcomp(&expression, in, REG_EXTENDED | REG_NEWLINE | REG_ICASE);
		} else if (flagM) {
			reti = regcomp(&expression, in, REG_EXTENDED | REG_NEWLINE);
		} else {
			reti = regcomp(&expression, in, REG_EXTENDED | REG_ICASE);
		}

	// No flag compilation
	} else {
		reti = regcomp(&expression, in, REG_EXTENDED);
	}

	if (reti == 0) { // Compilation was successful

		const char * out = output.c_str();
		reti = regexec(&expression, out, 0, NULL, 0);

		if (reti == 0) { // Match
			return true;
		} else if (reti == REG_NOMATCH){ // No match
			return false;

		} else { // Memory Error
			Evaluation* p_ErrorTest = Evaluation::getSinglenton();
			string errorType = string((L->langEvaluate(6)).c_str()) + string(errorCase);
			const char* flagError = errorType.c_str();
			p_ErrorTest->addFatalError(flagError);
			p_ErrorTest->outputEvaluation();
			abort();
		}

	} else { // Compilation error
		size_t length = regerror(reti, &expression, NULL, 0);
        char* bff = new char[length + 1];
        (void) regerror(reti, &expression, bff, length);
		Evaluation* p_ErrorTest = Evaluation::getSinglenton();
		string errorType = string((L->langEvaluate(7)).c_str()) + string((L->langEvaluate(8)).c_str()) + string(errorCase) + string(".\n")+ string(bff);
		const char* flagError = errorType.c_str();
		p_ErrorTest->addFatalError(flagError);
		p_ErrorTest->outputEvaluation();
		abort();
		return false;
	}
}

// Returns the expression without flags nor '/'
string RegularExpressionOutput::studentOutputExpected() {return cleanText;}

OutputChecker* RegularExpressionOutput::clone() {
	return new RegularExpressionOutput(outputExpected(), errorCase);
}

// Tests if it's a regular expression. A regular expressions should be between /../
bool RegularExpressionOutput::typeMatch(const string& text) {
	string clean=Tools::trim(text);
	if (clean.size() > 2 && clean[0] == '/') {
		for (size_t i = 1; i < clean.size(); i++) {
			if (clean[i] == '/') {
				return true;
			}
		}
	}
	return false;
}

string RegularExpressionOutput::type() {
	return (L->langEvaluate(9)).c_str();
}
/**
 * Class Case Definitions
 * Case represents cases
 */
Case::Case() {
	reset();
}

void Case::reset() {
	input = "";
	output.clear();
	caseDescription = "";
	gradeReduction = std::numeric_limits<float>::min();
	failMessage = "";
	programToRun = "";
	programArgs = "";
	variation = "";
	expectedExitCode = std::numeric_limits<int>::min();
}

void Case::addInput(string s) {
	input += s;
}

string Case::getInput() {
	return input;
}

void Case::addOutput(string o) {
	output.push_back(o);
}

const vector< string > & Case::getOutput() {
	return output;
}

void Case::setFailMessage(const string &s) {
	failMessage = s;
}

string Case::getFailMessage() {
	return failMessage;
}
void Case::setCaseDescription(const string &s) {
	caseDescription = s;
}

string Case::getCaseDescription() {
	return caseDescription;
}
void Case::setGradeReduction(float g) {
	gradeReduction = g;
}

float Case::getGradeReduction() {
	return gradeReduction;
}

void Case::setExpectedExitCode(int e) {
	expectedExitCode = e;
}

int Case::getExpectedExitCode() {
	return expectedExitCode;
}
void Case::setProgramToRun(const string &s) {
	programToRun = s;
}

string Case::getProgramToRun() {
	return programToRun;
}

void Case::setProgramArgs(const string &s) {
	programArgs = s;
}

string Case::getProgramArgs() {
	return programArgs;
}

void Case::setVariation(const string &s) {
	variation = Tools::toLower(Tools::trim(s));
}

string Case::getVariation() {
	return variation;
}

/**
 * Class TestCase Definitions
 * TestCase represents cases of test
 */

void TestCase::cutOutputTooLarge(string &output) {
	if (output.size() > MAXOUTPUT) {
		outputTooLarge = true;
		output.erase(0, output.size() - MAXOUTPUT);
	}
}

void TestCase::readWrite(int fdread, int fdwrite) {
	const int MAX = 1024* 10 ;
	// Buffer size to read
	const int POLLREAD = POLLIN | POLLPRI;
	// Poll to read from program
	struct pollfd devices[2];
	devices[0].fd = fdread;
	devices[1].fd = fdwrite;
	char buf[MAX];
	devices[0].events = POLLREAD;
	devices[1].events = POLLOUT;
	int res = poll(devices, programInput.size()>0?2:1, 0);
	if (res == -1) // Error
		return;
	if (res == 0) // Nothing to do
		return;
	if (devices[0].revents & POLLREAD) { // Read program output
		int readed = read(fdread, buf, MAX);
		if (readed > 0) {
			sizeReaded += readed;
			if (programInput.size() > 1) {
				programOutputBefore += string(buf, readed);
				cutOutputTooLarge(programOutputBefore);
			} else {
				programOutputAfter += string(buf, readed);
				cutOutputTooLarge(programOutputAfter);
			}
		}
	}
	if (programInput.size() > 0 && devices[1].revents & POLLOUT) { // Write to program
		int written = write(fdwrite, programInput.c_str(), Tools::nextLine(
				programInput));
		if (written > 0) {
			programInput.erase(0, written);
		}
		if(programInput.size()==0){
			close(fdwrite);
		}
	}
}

void TestCase::addOutput(const string &o, const string &actualCaseDescription){
// actualCaseDescripction, used to get current test name for Output recognition
	if(ExactTextOutput::typeMatch(o))
		this->output.push_back(new ExactTextOutput(o));
	else if (RegularExpressionOutput::typeMatch(o))
		this->output.push_back(new RegularExpressionOutput(o, actualCaseDescription));
	else if(NumbersOutput::typeMatch(o))
		this->output.push_back(new NumbersOutput(o));
	else
		this->output.push_back(new TextOutput(o));
}

void TestCase::setEnvironment(const char **environment) {
	envv = environment;
}

void TestCase::setDefaultCommand() {
	command = "./vpl_test";
	argv = new const char*[2];
	argv[0] = command;
	argv[1] = NULL;
}

TestCase::TestCase(const TestCase &o) {
	id=o.id;
	correctOutput=o.correctOutput;
	correctExitCode = o.correctExitCode;
	outputTooLarge=o.outputTooLarge;
	programTimeout=o.programTimeout;
	executionError=o.executionError;
	strcpy(executionErrorReason,o.executionErrorReason);
	sizeReaded=o.sizeReaded;
	input=o.input;
	caseDescription=o.caseDescription;
	gradeReduction=o.gradeReduction;
	expectedExitCode = o.expectedExitCode;
	exitCode = o.exitCode;
	failMessage=o.failMessage;
	programToRun=o.programToRun;
	programArgs=o.programArgs;
	gradeReductionApplied=o.gradeReductionApplied;
	programOutputBefore=o.programOutputBefore;
	programOutputAfter=o.programOutputAfter;
	programInput=o.programInput;
	for(size_t i = 0; i < o.output.size(); i++){
		output.push_back(o.output[i]->clone());
	}
	setDefaultCommand();
}

TestCase& TestCase::operator=(const TestCase &o) {
	id=o.id;
	correctOutput=o.correctOutput;
	correctExitCode = o.correctExitCode;
	outputTooLarge=o.outputTooLarge;
	programTimeout=o.programTimeout;
	executionError=o.executionError;
	strcpy(executionErrorReason,o.executionErrorReason);
	sizeReaded=o.sizeReaded;
	input=o.input;
	caseDescription=o.caseDescription;
	gradeReduction=o.gradeReduction;
	failMessage=o.failMessage;
	programToRun=o.programToRun;
	programArgs=o.programArgs;
	expectedExitCode = o.expectedExitCode;
	exitCode = o.exitCode;
	gradeReductionApplied=o.gradeReductionApplied;
	programOutputBefore=o.programOutputBefore;
	programOutputAfter=o.programOutputAfter;
	programInput=o.programInput;
	for(size_t i=0; i<output.size(); i++)
		delete output[i];
	output.clear();
	for(size_t i=0; i<o.output.size(); i++){
		output.push_back(o.output[i]->clone());
	}
	return *this;
}

TestCase::~TestCase() {
	for(size_t i = 0; i < output.size(); i++)
		delete output[i];
}

TestCase::TestCase(int id, const string &input, const vector<string> &output,
		const string &caseDescription, const float gradeReduction,
		string failMessage, string programToRun, string programArgs, int expectedExitCode) {
	this->id = id;
	this->input = input;
	for(size_t i = 0; i < output.size(); i++){
		addOutput(output[i], caseDescription);
	}
	this->caseDescription = caseDescription;
	this->gradeReduction = gradeReduction;
	this->expectedExitCode = expectedExitCode;
	this->programToRun = programToRun;
	this->programArgs = programArgs;
	this->failMessage = failMessage;
	exitCode = std::numeric_limits<int>::min();
	outputTooLarge = false;
	programTimeout = false;
	executionError = false;
	correctOutput = false;
	correctExitCode = false;
	sizeReaded = 0;
	gradeReductionApplied =0;
	strcpy(executionErrorReason, "");
	setDefaultCommand();
}

bool TestCase::isCorrectResult() {
	bool correct = correctOutput &&
			      ! programTimeout &&
				  ! outputTooLarge &&
				  ! executionError;
	return correct || (isExitCodeTested() && correctExitCode);
}

bool TestCase::isExitCodeTested() {
	return expectedExitCode != std::numeric_limits<int>::min();
}

float TestCase::getGradeReduction() {
	return gradeReduction;
}

void TestCase::setGradeReductionApplied(float r) {
	gradeReductionApplied=r;
}

float TestCase::getGradeReductionApplied() {
	return gradeReductionApplied;
}

string TestCase::getCaseDescription(){
	return caseDescription;
}

string TestCase::getCommentTitle(bool withGradeReduction=false) {
	char buf[100];
	string ret;
	sprintf(buf, (L->langEvaluate(10)).c_str(), id);
	ret = buf;
	if (caseDescription.size() > 0) {
		ret += ": " + caseDescription;
	}
	if(withGradeReduction && getGradeReductionApplied()>0){
		sprintf(buf," (%.3f)", -getGradeReductionApplied());
		ret += buf;
	}
	ret += '\n';
	return ret;
}

string TestCase::getComment() {
	if (isCorrectResult()) {
		return "";
	}
	char buf[100];
	string ret;
	if(output.size()==0){
		ret += (L->langEvaluate(11)).c_str();
	}
	if (programTimeout) {
		ret += (L->langEvaluate(12)).c_str();
	}
	if (outputTooLarge) {
		sprintf(buf, (L->langEvaluate(13)).c_str(), sizeReaded / 1024);
		ret += buf;
	}
	if (executionError) {
		ret += executionErrorReason + string("\n");
	}
	if (isExitCodeTested() && ! correctExitCode) {
		char buf[250];
		sprintf(buf, (L->langEvaluate(14)).c_str(), expectedExitCode, exitCode);
		ret += buf;
	}
	if (! correctOutput) {
		if (failMessage.size()) {
			ret += failMessage + "\n";
		} else {
			ret += (L->langEvaluate(15)).c_str();
			ret += (L->langEvaluate(16)).c_str();
			ret += Tools::caseFormat(input);
			ret += (L->langEvaluate(17)).c_str();
			ret += Tools::caseFormat(programOutputBefore + programOutputAfter);
			if(output.size()>0){
				ret += (L->langEvaluate(18)).c_str()+output[0]->type()+")\n";
				ret += Tools::caseFormat(output[0]->studentOutputExpected());
			}
		}
	}
	return ret;
}

void TestCase::splitArgs(string programArgs) {
	int l = programArgs.size();
	int nargs = 1;
	char *buf = new char[programArgs.size() + 1];
	strcpy(buf, programArgs.c_str());
	argv = (const char **) new char*[programArgs.size() + 1];
	argv[0] = command;
	bool inArg = false;
	char separator = ' ';
	for(int i=0; i < l; i++) { // TODO improve
		if ( ! inArg ) {
			if ( buf[i] == ' ' ) {
				buf[i] = '\0';
				continue;
			} else if ( buf[i] == '\'' ) {
				argv[nargs++] = buf + i + 1;
				separator = '\'';
			} else if ( buf[i] == '"' ) {
				argv[nargs++] = buf + i + 1;
				separator = '"';
			} else if ( buf[i] != '\0') {
				argv[nargs++] = buf + i;
				separator = ' ';
			}
			inArg = true;
		} else {
			if ( buf[i] == separator  ) {
				buf[i] = '\0';
				separator = ' ';
				inArg = false;
			}
		}
	}
	argv[nargs] = NULL;
}

void TestCase::runTest(time_t timeout) {// Timeout in seconds
	time_t start = time(NULL);
	int pp1[2]; // Send data
	int pp2[2]; // Receive data
	if (pipe(pp1) == -1 || pipe(pp2) == -1) {
		executionError = true;
		sprintf(executionErrorReason, (L->langEvaluate(19)).c_str(),
				strerror(errno));
		return;
	}
	if ( programToRun > "" && programToRun.size() < 512) {
		command = programToRun.c_str();
	}
	if ( ! Tools::existFile(command) ){
		executionError = true;
		sprintf(executionErrorReason, (L->langEvaluate(20)).c_str(), command);
		return;
	}
	pid_t pid;
	if ( programArgs.size() > 0) {
		splitArgs(programArgs);
	}
	if ((pid = fork()) == 0) {
		// Execute
		close(pp1[1]);
		dup2(pp1[0], STDIN_FILENO);
		close(pp2[0]);
		dup2(pp2[1], STDOUT_FILENO);
		dup2(STDOUT_FILENO, STDERR_FILENO);
		setpgrp();
		execve(command, (char * const *) argv, (char * const *) envv);
		perror((L->langEvaluate(21)).c_str());
		abort(); //end of child
	}
	if (pid == -1) {
		executionError = true;
		sprintf(executionErrorReason, (L->langEvaluate(22)).c_str(),
				strerror(errno));
		return;
	}
	close(pp1[0]);
	close(pp2[1]);
	int fdwrite = pp1[1];
	int fdread = pp2[0];
	Tools::fdblock(fdwrite, false);
	Tools::fdblock(fdread, false);
	programInput = input;
	if(programInput.size()==0){ // No input
		close(fdwrite);
	}
	programOutputBefore = "";
	programOutputAfter = "";
	pid_t pidr;
	int status;
	exitCode = std::numeric_limits<int>::min();
	while ((pidr = waitpid(pid, &status, WNOHANG | WUNTRACED)) == 0) {
		readWrite(fdread, fdwrite);
		usleep(5000);
		// TERMSIG or timeout or program output too large?
		if (Stop::isTERMRequested() || (time(NULL) - start) >= timeout
				|| outputTooLarge) {
			if ((time(NULL) - start) >= timeout) {
				programTimeout = true;
			}
			kill(pid, SIGTERM); // Send SIGTERM normal termination
			int otherstatus;
			usleep(5000);
			if (waitpid(pid, &otherstatus, WNOHANG | WUNTRACED) == pid) {
				break;
			}
			if (kill(pid, SIGQUIT) == 0) { // Kill
				break;
			}
		}
	}
	if (pidr == pid) {
		if (WIFSIGNALED(status)) {
			int signal = WTERMSIG(status);
			executionError = true;
			sprintf(executionErrorReason,
					(L->langEvaluate(23)).c_str(), strsignal(
							signal), signal);
		}
		if (WIFEXITED(status)) {
			exitCode = WEXITSTATUS(status);
		} else {
			executionError = true;
			strcpy(executionErrorReason,
					(L->langEvaluate(24)).c_str());
		}
	} else if (pidr != 0) {
		executionError = true;
		strcpy(executionErrorReason, (L->langEvaluate(25)).c_str());
	}
	readWrite(fdread, fdwrite);
	correctExitCode = isExitCodeTested() && expectedExitCode == exitCode;
	correctOutput = match(programOutputAfter)
			     || match(programOutputBefore + programOutputAfter);
}

bool TestCase::match(string data) {
	for (size_t i = 0; i < output.size(); i++)
		if (output[i]->match(data))
			return true;
	return false;
}

/**
 * Class Evaluation Definitions
 */

Evaluation::Evaluation() {
	grade = 0;
	ncomments = 0;
	nerrors = 0;
	nruns = 0;
	noGrade = true;
}

Evaluation* Evaluation::getSinglenton() {
	if (singlenton == NULL) {
		singlenton = new Evaluation();
	}
	return singlenton; // Fixes by Jan Derriks
}

void Evaluation::deleteSinglenton(){
	if (singlenton != NULL) {
		delete singlenton;
		singlenton = NULL;
	}
}

void Evaluation::addTestCase(Case &caso) {
	if ( caso.getVariation().size() && caso.getVariation() != variation ) {
		return;
	}
	testCases.push_back(TestCase(testCases.size() + 1, caso.getInput(), caso.getOutput(),
			caso.getCaseDescription(), caso.getGradeReduction(), caso.getFailMessage(),
			caso.getProgramToRun(), caso.getProgramArgs(), caso.getExpectedExitCode() ));
}

void Evaluation::removeLastNL(string &s) {
	if (s.size() > 0 && s[s.size() - 1] == '\n') {
		s.resize(s.size() - 1);
	}
}

bool Evaluation::cutToEndTag(string &value, const string &endTag) {
	size_t pos;
	if (endTag.size() && (pos = value.find(endTag)) != string::npos) {
		value.resize(pos);
		return true;
	}
	return false;
}

void Evaluation::loadTestCases(string fname) {
	if(!Tools::existFile(fname)) return;
	const char *CASE_TAG = "case=";
	const char *INPUT_TAG = "input=";
	const char *INPUT_END_TAG = "inputend=";
	const char *OUTPUT_TAG = "output=";
	const char *OUTPUT_END_TAG = "outputend=";
	const char *GRADEREDUCTION_TAG = "gradereduction=";
	const char *FAILMESSAGE_TAG = "failmessage=";
	const char *PROGRAMTORUN_TAG = "programtorun=";
	const char *PROGRAMARGS_TAG = "programarguments=";
	const char *EXPECTEDEXITCODE_TAG = "expectedexitcode=";
	const char *VARIATION_TAG = "variation=";
	enum {
		regular, ininput, inoutput
	} state;
	bool inCase = false;
	vector<string> lines = Tools::splitLines(Tools::readFile(fname));
    remove(fname.c_str());
	string inputEnd = "";
	string outputEnd = "";
	Case caso;
	string output = "";
	string tag, value;
	/* must be changed from String
	 * to pair type (regexp o no) and string. */
	state = regular;
	int nlines = lines.size();
	for (int i = 0; i < nlines; i++) {
		string &line = lines[i];
		Tools::parseLine(line, tag, value);
		if (state == ininput) {
			if (inputEnd.size()) { // Check for end of input.
				size_t pos = line.find(inputEnd);
				if (pos == string::npos) {
					caso.addInput(line + "\n");
				} else {
					cutToEndTag(line, inputEnd);
					caso.addInput(line);
					state = regular;
					continue; // Next line.
				}
			} else if (tag.size() && (tag == OUTPUT_TAG || tag
					== GRADEREDUCTION_TAG || tag == CASE_TAG)) {// New valid tag.
				state = regular;
				// Go on to process the current tag.
			} else {
				caso.addInput(line + "\n");
				continue; // Next line.
			}
		} else if (state == inoutput) {
			if (outputEnd.size()) { // Check for end of output.
				size_t pos = line.find(outputEnd);
				if (pos == string::npos) {
					output += line + "\n";
				} else {
					cutToEndTag(line, outputEnd);
					output += line;
					caso.addOutput(output);
					output = "";
					state = regular;
					continue; // Next line.
				}
			} else if (tag.size() && (tag == INPUT_TAG || tag == OUTPUT_TAG
					|| tag == GRADEREDUCTION_TAG || tag == CASE_TAG)) {// New valid tag.
				removeLastNL(output);
				caso.addOutput(output);
				output = "";
				state = regular;
			} else {
				output += line + "\n";
				continue; // Next line.
			}
		}
		if (state == regular && tag.size()) {
			if (tag == INPUT_TAG) {
				inCase = true;
				if (cutToEndTag(value, inputEnd)) {
					caso.addInput(value);
				} else {
					state = ininput;
					caso.addInput(value + '\n');
				}
			} else if (tag == OUTPUT_TAG) {
				inCase = true;
				if (cutToEndTag(value, outputEnd))
					caso.addOutput(value);
				else {
					state = inoutput;
					output = value + '\n';
				}
			} else if (tag == GRADEREDUCTION_TAG) {
				inCase = true;
				value = Tools::trim(value);
				// A percent value?
				if( value.size() > 1 && value[ value.size() - 1 ] == '%' ){
					float percent = atof(value.c_str());
					caso.setGradeReduction((grademax-grademin)*percent/100);
				}else{
					caso.setGradeReduction( atof(value.c_str()) );
				}
			} else if (tag == EXPECTEDEXITCODE_TAG) {
				caso.setExpectedExitCode( atoi(value.c_str()) );
			} else if (tag == PROGRAMTORUN_TAG) {
				caso.setProgramToRun(Tools::trim(value));
			} else if (tag == PROGRAMARGS_TAG) {
				caso.setProgramArgs(Tools::trim(value));
			} else if (tag == FAILMESSAGE_TAG) {
				caso.setFailMessage(Tools::trim(value));
			} else if (tag == VARIATION_TAG) {
				caso.setVariation(value);
			} else if (tag == INPUT_END_TAG) {
				inputEnd = Tools::trim(value);
			} else if (tag == OUTPUT_END_TAG) {
				outputEnd = Tools::trim(value);
			} else if (tag == CASE_TAG) {
				if (inCase) {
					addTestCase(caso);
					caso.reset();
				}
				inCase = true;
				caso.setCaseDescription( Tools::trim(value) );
			} else {
				if ( line.size() > 0 ) {
					char buf[250];
					sprintf(buf,(L->langEvaluate(26)).c_str(), i+1);
					addFatalError(buf);
				}
			}
		}
	}
	// TODO review
	if (state == inoutput) {
		removeLastNL(output);
		caso.addOutput(output);
	}
	if (inCase) { // Last case => save current.
		addTestCase(caso);
	}
}

bool Evaluation::loadParams() {
	grademin= Tools::getenv("VPL_GRADEMIN", 0.0);
	grademax = Tools::getenv("VPL_GRADEMAX", 10);
	maxtime = (int) Tools::getenv("VPL_MAXTIME", 20);
	variation = Tools::toLower(Tools::trim(Tools::getenv("VPL_VARIATION","")));
	noGrade = grademin >= grademax;
	return true;
}

void Evaluation::addFatalError(const char *m) {
	float reduction = grademax - grademin;
	if (ncomments >= MAXCOMMENTS)
		ncomments = MAXCOMMENTS - 1;

	snprintf(titles[ncomments], MAXCOMMENTSTITLELENGTH, "%s", m);
	snprintf(titlesGR[ncomments], MAXCOMMENTSTITLELENGTH, "%s (%.2f)", m, reduction);
	strcpy(comments[ncomments], "");
	ncomments ++;
	grade = grademin;
}

void Evaluation::runTests() {
	if (testCases.size() == 0) {
		return;
	}
	if (maxtime < 0) {
		addFatalError((L->langEvaluate(27)).c_str());
		return;
	}
	nerrors = 0;
	nruns = 0;
	grade = grademax;
	float defaultGradeReduction = (grademax - grademin) / testCases.size();
	int timeout = maxtime / testCases.size();
	for (size_t i = 0; i < testCases.size(); i++) {
		printf((L->langEvaluate(28)).c_str(), (unsigned long) i+1, (unsigned long)testCases.size(), testCases[i].getCaseDescription().c_str());
		if (timeout <= 1 || Timer::elapsedTime() >= maxtime) {
			grade = grademin;
			addFatalError((L->langEvaluate(27)).c_str());
			return;
		}
		if (maxtime - Timer::elapsedTime() < timeout) { // Try to run last case
			timeout = maxtime - Timer::elapsedTime();
		}
		testCases[i].runTest(timeout);
		nruns++;
		if (!testCases[i].isCorrectResult()) {
			if (Stop::isTERMRequested())
				break;
			float gr = testCases[i].getGradeReduction();
			if (gr == std::numeric_limits<float>::min())
				testCases[i].setGradeReductionApplied(defaultGradeReduction);
			else
				testCases[i].setGradeReductionApplied(gr);
			grade -= testCases[i].getGradeReductionApplied();
			if (grade < grademin) {
				grade = grademin;
			}
			nerrors++;
			if(ncomments<MAXCOMMENTS){
				strncpy(titles[ncomments], testCases[i].getCommentTitle().c_str(),
						MAXCOMMENTSTITLELENGTH);
				strncpy(titlesGR[ncomments], testCases[i].getCommentTitle(true).c_str(),
						MAXCOMMENTSTITLELENGTH);
				strncpy(comments[ncomments], testCases[i].getComment().c_str(),
						MAXCOMMENTSLENGTH);
				ncomments++;
			}
		}
	}
}

// WIP
void Evaluation::outputEvaluationEnhance() {
	const char* stest[] = {(L->langEvaluate(29)).c_str(), (L->langEvaluate(30)).c_str()};
	if (testCases.size() == 0) {
		printf("<|--\n");
		printf("%s", (L->langEvaluate(36)).c_str());
		printf("--|>\n");
	}
	if (ncomments > 1) {
		printf("\n<|--\n");
		printf("%s", (L->langEvaluate(31)).c_str());
		for (int i = 0; i < ncomments; i++) {
			printf("<comment>%s", titles[i]);
		}
		printf("--|>\n");
	}
	if ( ncomments > 0 ) {
		printf("\n<|--\n");
		for (int i = 0; i < ncomments; i++) {
			printf("<subTitle>%s", titlesGR[i]);
			printf("%s\n", comments[i]);
		}
		printf("--|>\n");
	}
	int passed = nruns - nerrors;
	if ( nruns > 0 ) {
		printf("%s",(L->langEvaluate(32)).c_str());
		printf("%s",(L->langEvaluate(33)).c_str());
		printf((L->langEvaluate(34)).c_str(),
				nruns, nruns==1?stest[0]:stest[1],
				passed, passed==1?stest[0]:stest[1]); // Taken from Dominique Thiebaut
		printf("%s",(L->langEvaluate(33)).c_str());
		printf("\n--|>\n");
	}
	if ( ! noGrade ) {
		char buf[100];
		sprintf(buf, "%5.2f", grade);
		int len = strlen(buf);
		if (len > 3 && strcmp(buf + (len - 3), ".00") == 0)
			buf[len - 3] = 0;
		printf((L->langEvaluate(35)).c_str(), buf);
	}
	fflush(stdout);
}

void Evaluation::outputEvaluation() {
	const char* stest[] = {" test", "tests"};
	if (testCases.size() == 0) {
		printf("<|--\n");
		printf("-No test case found\n");
		printf("--|>\n");
	}
	if (ncomments > 1) {
		printf("\n<|--\n");
		printf("-Failed tests\n");
		for (int i = 0; i < ncomments; i++) {
			printf("%s", titles[i]);
		}
		printf("--|>\n");
	}
	if ( ncomments > 0 ) {
		printf("\n<|--\n");
		for (int i = 0; i < ncomments; i++) {
			printf("-%s", titlesGR[i]);
			printf("%s\n", comments[i]);
		}
		printf("--|>\n");
	}
	int passed = nruns - nerrors;
	if ( nruns > 0 ) {
		printf("\n<|--\n");
		printf("-Summary of tests\n");
		printf(">+------------------------------+\n");
		printf(">| %2d %s run/%2d %s passed |\n",
				nruns, nruns==1?stest[0]:stest[1],
				passed, passed==1?stest[0]:stest[1]); // Taken from Dominique Thiebaut
		printf(">+------------------------------+\n");
		printf("\n--|>\n");
	}
	if ( ! noGrade ) {
		char buf[100];
		sprintf(buf, "%5.2f", grade);
		int len = strlen(buf);
		if (len > 3 && strcmp(buf + (len - 3), ".00") == 0)
			buf[len - 3] = 0;
		printf("\nGrade :=>>%s\n", buf);
	}
	fflush(stdout);
}

void nullSignalCatcher(int n) {
	//printf("Signal %d\n",n);
}

void signalCatcher(int n) {
	//printf("Signal %d\n",n);
	if (Stop::isTERMRequested()) {
		Evaluation* obj = Evaluation::getSinglenton();
		obj->outputEvaluation();
		abort();
	}
	Evaluation *obj = Evaluation::getSinglenton();
	if (n == SIGTERM) {
		obj->addFatalError((L->langEvaluate(37)).c_str());
	} else {
		obj->addFatalError((L->langEvaluate(38)).c_str());
		obj->outputEvaluation();
		Stop::setTERMRequested();
		abort();
	}
	alarm(1);
}

void setSignalsCatcher() {
	// Removes as many signal controllers as possible
	for(int i=0;i<31; i++)
		signal(i, nullSignalCatcher);
	signal(SIGINT, signalCatcher);
	signal(SIGQUIT, signalCatcher);
	signal(SIGILL, signalCatcher);
	signal(SIGTRAP, signalCatcher);
	signal(SIGFPE, signalCatcher);
	signal(SIGSEGV, signalCatcher);
	signal(SIGALRM, signalCatcher);
	signal(SIGTERM, signalCatcher);
}

int main(int argc, char *argv[], char **envp) {

	// get enviroment variables
	char* e = getenv("VPL_ENHANCE");
	string enhance_env((e==nullptr)?"":e);
  
	may_enhance = 
	(enhance_env=="true"||enhance_env=="TRUE");

	string file0(getenv("VPL_SUBFILE0"));
	vector<string> p = {ext_map.at(getFileExtension(file0))};
	string lang(lang_map.at(getenv("VPL_LANG")));

	// load error messages
	L = new Interface( p, lang);

	if (!L->loadTransLangLib()){
		fprintf(stderr, "loadTransLangLib fail");
		return EXIT_FAILURE;
	}
	if (!L->loadEnhacedLangLib()){
		fprintf(stderr, "loadEnhacedLangLib fail");
		return EXIT_FAILURE;
	}

	Timer::start();
	TestCase::setEnvironment((const char**) envp);
	setSignalsCatcher();
	Evaluation* obj = Evaluation::getSinglenton();
	obj->loadParams();
	obj->loadTestCases("evaluate.cases");
	obj->runTests();
	obj->outputEvaluation();
	delete L;

	return EXIT_SUCCESS;
}
