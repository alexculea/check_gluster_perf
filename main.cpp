/*
   Gluster FS Performance Nagios/Icinga Check

   by Alexandru Culea
   No rights reserved. You may freely modify, 
   distribute, sell or use in commercial applications.
   
   No warranty is provided for any efects this program 
   could cause.

   The code depends on C++11 libs/semantics
*/


// all the good stuff's in here
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <exception>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <cmath>
#include "json/src/json.hpp"

using namespace std;
using json = nlohmann::json;

/* Time unit used to process performance metrics */
enum class UnitType : short {
    Microseconds, Miliseconds, Seconds
};

/* in-memory storage type of the processed argument */
enum class ParamType : short {
    Bool, Int, Double, Float, String, Short
};

struct Metric {
    double      value;
    UnitType    unit;
};

union ParamValue {
    bool b;
    int i;
    char c;
    short s;
    float f;
    double d;
    const char* str;
};

/*
    Defines the constraings and requirements that can apply to a stdin argument
*/
class Argument {
public:
    ParamValue* pStorage                        = nullptr; // a pointer to the variable where the parsed/transformed/validated value will be stored
    string      format_regex                    = ""; // regex pattern used to test the validation of the parameter value
    ParamType   type                            = ParamType::Int; // enum type that establishes the storage type (in pStorage)
    map<string, ParamValue>* pPossible_values   = nullptr; // map of possible argument values and their processed value
    bool        required                        = false;   // the read_arguments will throw if an argument with this flag has not been put in stdin
    bool        multivalue                      = false;
    char        value_separator                 = ',';

    Argument() {};
    Argument(ParamValue* storage, ParamType type, const string& regex_pattern = "", bool required=false, map<string, ParamValue>* possible_values = nullptr, bool multivalue=false)
    {
        this->pStorage = storage;
        this->type = type;
        this->format_regex = regex_pattern;
        this->required = required;
        this->pPossible_values = possible_values;
        this->multivalue = multivalue;
    }
};

/* Global parameters set by read_args */
ParamValue  g_warning           = { .d=0.0};      //the warning threshold in g_unit_type_input units (ms/s/us)
ParamValue  g_critical          = { .d=0.0};      //the critical threshold in g_unit_type_input units (ms/s/us)
ParamValue  g_volname           = { .str=""};       //the GlusterFS volume name to monitor
ParamValue  g_unit_type_input   = { .s = (short) UnitType::Microseconds};  //the unit type specified for input. possible values: ms/s/us
ParamValue  g_unit_type_output  = { .s = (short) UnitType::Microseconds};  //the unit type specified for output. possible values: ms/s/us
ParamValue  g_gluster_unit_type = { .s = (short) UnitType::Microseconds};  //the unit GlusterFS outputs its stats in, as of version 3.9 it's microseconds, should they change it we can too! At runtime!
ParamValue  g_report_error_unkn = { .b = false } ;    //if true, program errros are reported as UNKNOW state to Nagios. otherwise as CRITICAL
ParamValue  g_gluster_stats_file= { .str=""};       // location of the GlusterFS stats dump file
ParamValue  g_max_file_age      = { .i = 5 };       // maximum age of the stats dump file, if exceeded the program will report critical

int         g_error_ret_val     = 2;        // default value 2 - CRITICAL
int         g_program_ret_val   = 0;        // whatever is here will be returned from main()

// Possible values for -u and -ou and their final value
map<string, ParamValue> g_unit_enum_map = {
    {string("us"), { .s=(short) UnitType::Microseconds}}, // the .s initializes the s field of the ParamValue union
    {string("ms"), { .s=(short) UnitType::Miliseconds}},
    {string("s"),  { .s=(short) UnitType::Seconds}}    
};

// Possible values for parameter --report-errors-unknown
map<string, ParamValue> g_report_errors_enum_map = {
    {string("yes"), { .b=true}},
    {string("no"),  { .b=false}}
};


/* 
This map defines possible arguments and their properties as defined in the Argument class.
It serves as input to read_arguments function, used to process the stdin arguments.

To add a new supported parameter, simply define its storage variable as ParamValue, add the said
parameter to the list below and you'll have it available after read_arguments was invoked
*/
map<string, Argument> g_arg_map = {
// = stdin argument ======== storage pointer ====== storage type ===== validation regex=    requred === input-processing-map========
    {string("-w"),  Argument(&g_warning,            ParamType::Double,  "^[0-9\\.]+$",      true,       nullptr) },
    {string("-c"),  Argument(&g_critical,           ParamType::Double,  "^[0-9\\.]+$",      true,       nullptr) },
    {string("-v"),  Argument(&g_volname,            ParamType::String,  ".*",               true,       nullptr) },
    {string("--override-stats-file"),  
                    Argument(&g_gluster_stats_file, ParamType::String,  ".*",               false,      nullptr) },
    {string("-u"),  Argument(&g_unit_type_input,    ParamType::Short,   "(ms|us|s)",        false,      &g_unit_enum_map) },
    {string("-ou"), Argument(&g_unit_type_output,   ParamType::Short,   "(ms|us|s)",        false,      &g_unit_enum_map) },
    {string("--gluster-stats-unit"), 
                    Argument(&g_gluster_unit_type,  ParamType::Short,   "(ms|us|s)",        false,      &g_unit_enum_map) },
    {string("--report-errors-unknown"), 
                    Argument(&g_report_error_unkn,  ParamType::Bool,    "(yes|no)",         false,      &g_report_errors_enum_map) },
    {string("--max-file-age-minutes"), 
                    Argument(&g_max_file_age,       ParamType::Short,   "^[0-9\\.]+$",      false,      nullptr) }                    
};


void        display_help    (); 
void        read_arguments  (const vector<string>& arguments, const map<string, Argument>&) throw (logic_error, invalid_argument);
bool        is_valid        (const string& pattern, const string& value);
ParamValue  transform       (const string& input, ParamType type, const map<string, ParamValue>* transform_map, ParamValue not_found_val) throw (logic_error);
Metric      convert         (const Metric& src, const UnitType dst_unit);
void        read_json_dump  (const string& filepath, json& results);

int main(int argc, char** argv)
{
     // create a JSON array
    json j1 = {"one", "two", 3, 4.5, false};
    vector<string> stdin_args(argv, argv + argc); // neat trick to copy all arguments in a vector of strings

    if (argc > 1 && stdin_args[1] == "/?")  // if we have at least 1 argument defined and that is "/?"
    {        
        display_help();
        return 0;     
    }

    try 
    {
        // start by setting the return value to error. 
        // if the execution reaches the end, we'll then set to success
        g_program_ret_val = g_error_ret_val; 

        read_arguments(stdin_args, g_arg_map);


        g_program_ret_val = 0;
    } 
    catch (const invalid_argument& ia)
    {   
        cout << "Invalid paramteres/arguments: " << ia.what() << endl;
    }
    catch (const logic_error& le)
    {
        cout << "Program logic exception: " << le.what() << endl;
    } 
    catch (const exception& e)
    {
        cout << "Unexpected program exception: " << e.what() << endl;
    } 

    return g_program_ret_val;
}

/*
Looks at arguments in "name value" format.
Specifically:
- loops through all arguments
- validates them against argmap
- make sure the values match the argmap regex
- stores them in argmap given pointers 
Will happily throw stuff
*/
void read_arguments(const vector<string>& arguments, const map<string, Argument>& argmap) throw (logic_error, invalid_argument)
{
    vector<string>      requiredArguments;
    const Argument*     pCurrentArgument = nullptr;
    ParamValue*         pStoreVariable = nullptr;  
    ostringstream       osError;

    // establish what arguments are required
    for (auto declared_arg = argmap.begin(); declared_arg != argmap.end(); declared_arg++)
    {
        if (declared_arg->second.required == true)
        {
            requiredArguments.push_back(declared_arg->first); // add the argument name to the list
        }
    }

    // loop through stdin provided arguments
    for (auto current_stdin_arg = arguments.begin()+1; current_stdin_arg < arguments.end(); current_stdin_arg++) // we start from position 1, 0 - is the name of the command invoked and we don't need that
    {
        
        
        auto elm = argmap.find(*current_stdin_arg); 
        if (elm != argmap.end()) // is our argument declared?
        {    
            pCurrentArgument = &elm->second;
            pStoreVariable = pCurrentArgument->pStorage;

            // if we have a value provided in the next argument
            if (current_stdin_arg+1 >= arguments.end())
            {
                osError << "Missing value for argument '" << *current_stdin_arg << "'";
                throw invalid_argument(osError.str());
            }

            // do we have where to store?
            if (pStoreVariable == nullptr)
            {
                osError << "No storage variable defined for parameter " << *current_stdin_arg;
                throw logic_error(osError.str());
            }
            
            auto arg_value = current_stdin_arg+1; // iterator pointing to the next element (value)
            
            if (! is_valid(pCurrentArgument->format_regex, *arg_value)) // regex matches?
            {
                osError << "Invalid value given for parameter '" << *current_stdin_arg << "'. Got '" << *arg_value << "', expected format (regex) : " << pCurrentArgument->format_regex;
                throw invalid_argument(osError.str());
            }

            // having checked everything let's do the transform
            *pStoreVariable = transform(*arg_value, pCurrentArgument->type, pCurrentArgument->pPossible_values, { .s=0 }); // map input string to pre-defined value                        
                              

            auto req_item_it = find(requiredArguments.begin(), requiredArguments.end(), *current_stdin_arg);
            // remove the argument from the list of required parameters
            if (req_item_it != requiredArguments.end())
            {
                requiredArguments.erase(req_item_it);
            }

            // move the iterator the next name argument, skipping the value we just processed
            advance(current_stdin_arg, 1); // -- this is std::advance coming from #include <iterator>
        } else { // otherwise, an unsupported argument was passed            
            throw invalid_argument(*current_stdin_arg); //  (╯°□°）╯︵ ┻━┻
        }
    }

    // if at the end we did NOT process all required arguments, THROW
    if (requiredArguments.size() > 0)
    {
        osError << "Required arguments not specified. Missing: ";
        for (auto missing_arg_iter = requiredArguments.begin(); missing_arg_iter < requiredArguments.end(); missing_arg_iter++)
        {
            osError << *missing_arg_iter;
            if (missing_arg_iter != requiredArguments.end()-1) // if this isn't last item
            {
                osError << ", ";
            }
        }

        throw invalid_argument(osError.str());
    }

    // the end
}

/*
Takes the input looks it up in the map as key, returns the leaf of the key if found, returns not_found_val otherwise.
*/
ParamValue transform(const string& input, ParamType type, const map<string, ParamValue>* transform_map, ParamValue not_found_val = { .s=0 }) throw (logic_error)
{
    if (transform_map != nullptr)
    {
        auto transformed_value_it = transform_map->find(input);
        if (transformed_value_it != transform_map->end())
        {
            return transformed_value_it->second;
        } else
        {
            return not_found_val;
        }
    } else {
        ParamValue retVal;

        switch (type)
        {
            case ParamType::Double: 
                retVal.d = stod(input); // stod = string to double - it's from the c++11 standard library
                break;      
            case ParamType::Float:
                retVal.f = stof(input);
                break;
            case ParamType::Int:
                retVal.i = stoi(input);
                break;
            case ParamType::String:
                retVal.str = input.c_str();
                break;   
            case ParamType::Bool:
                if (input == "TRUE" || input == "true" || input == "yes" || input == "YES")
                    retVal.b = true;                    
                else 
                    retVal.b = false;                    
                
                break;             
            default:
                throw logic_error("Type not implemented.");
        }


        return retVal;
    }
}

bool is_valid(const string& pattern, const string& value)
{
    regex p(pattern);
    smatch stdmatch;

    bool match = regex_match(value, p);
    return match;
}

Metric convert (const Metric& src, const UnitType dst_unit)
{
    map<UnitType, int> exp_map = {
        {UnitType::Microseconds, 6},
        {UnitType::Miliseconds,  3},
        {UnitType::Seconds,      0}
    };

    double  result      = 0.0;
    int     exp_diff    = exp_map[src.unit] - exp_map[dst_unit];

    if (exp_diff < 0)
    {
        result = src.value * pow(10, exp_diff);
    } 
    else if (exp_diff > 0)
    {
        result = src.value / pow(10, exp_diff);
    } 
    else
    {
        result = src.value;
    }

    return {result, dst_unit};
}

void display_help()
{
    char help_message[] = "\n"
"    The program gets the average latency times from GlusterFS \n"
"    for file operations on the monitored volume.\n"
"    Volume stat dumping needs to be enabled and started beforehand.\n"
" \n"
"    Quick Start:\n"
"        ./check_gluster_perf -w 50 -c 150 -u ms -v gluster_fs_volume_name --report-error-unknown yes\n"
" \n"
"    Usage:\n"
"      check_gluster_perf [-w <warning-level>] [-c <critical-level>] [-v <volume-name>] [-u <unit-type>] [-ou <unit-type>]\n"
"      \n"
"      -w        Warning threshold in -u units\n"
"                [required]  \n"
"                \n"
" \n"
"      -c        Critical threshold in -u units \n"
"                [required]\n"
" \n"
" \n"
"      -v        The name of the GlusterFS volume.\n"
"                [required]\n"
" \n"
"      -u        Unit type provided in input. Possible choices: us, ms, s, default: us\n"
"                [optional] \n"
" \n"
"      -ou       Output unit type. Accepts the same types as -u only they're used for\n"
"                output values. Can have independed value from -u\n"
"                [optional]\n"
" \n"
"      --report-errors-unknown\n"
"                if set to 'yes', script/component related errors are reported as UNKNOWN\n"
"                instead of CRITICAL. Default: CRITICAL\n"
"                [optional]\n"
" \n"
" ";

    printf(help_message);
}