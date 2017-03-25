/*
Gluster FS Performance Nagios/Icinga Check
by Alexandru Culea
   
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>

The code depends on C++11 libs/semantics
*/

const char PROGRAM_NAME[]       = "GlusterFS Performance Check";
const char PROGRAM_VERSION[]    = "1.0.0";

// all the good stuff's in here
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <exception>
#include <sstream>
#include <cmath>
#include <functional>
#include <regex>
#include <fstream>
#include <ctime>
#include "json/src/json.hpp"
#include "CmdParser/cmdparser.hpp"


#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

using json = nlohmann::json;

/* Time unit used to process performance metrics */
enum class UnitType : short {
    Microseconds, Miliseconds, Seconds

};

/* Nagios specific return codes */
enum class ReturnCode : int {
    OK          = 0, 
    Warning     = 1, 
    Critical    = 2, 
    Unknown     = 3
};


struct Metric {
    double      value;
    UnitType    unit;
};


class check_error : public std::runtime_error
{
    public:
        check_error(const std::string& what) : std::runtime_error(what){}
};

void        setup_cli_parameters(cli::Parser& parser);
Metric      convert         (const Metric& src, const UnitType dst_unit);
template <typename T> 
T           map_enum_to_value(const std::map<std::string, T>& map, const std::string& value) throw (std::invalid_argument);
std::time_t get_file_timestamp(std::string& path) throw (std::runtime_error);
std::string nagios_output_metrics(const std::map<std::string, Metric>& metrics, const Metric& warn, const Metric& crit);
void        read_json_dump(const std::string& file_path, std::vector<json>& results) throw (std::runtime_error);
ReturnCode  process_metrics(std::map<std::string, Metric>& exceeding_metrics, 
                            std::map<std::string, Metric>& performance_metrics,
                            Metric& total_average,
                            const std::vector<json>& dump_data, 
                            const Metric& warning_threshold, 
                            const Metric& critical_threshold, 
                            const UnitType& unit_type_output,
                            const UnitType& gluster_unit_type,
                            const std::regex& metric_filter, 
                            bool disable_threshold_comparison) throw(std::exception, std::runtime_error);


// Possible values for -u and -ou and their final value
std::map<std::string, UnitType> g_unit_enum_map = {
    {std::string("us"), UnitType::Microseconds}, // the .s initializes the s field of the ParamValue union
    {std::string("ms"), UnitType::Miliseconds},
    {std::string("s"),  UnitType::Seconds}    
};

std::map<UnitType, std::string> g_unit_enum_map_reverse = {
    {UnitType::Microseconds,    std::string("us")}, // the .s initializes the s field of the ParamValue union
    {UnitType::Miliseconds,     std::string("ms")},
    {UnitType::Seconds,         std::string("s") }    
};

bool g_verbose = false;

/*
Program Entry Point
===================

- configures CLI parameters
- checks if the help or version (-V) were passed, exits early with message
- puts parsed CLI parameters in their corresp. global variables
- looks at the GlusterFS dump file age and exists if its too old (parameter -dump-max-age-seconds)
- reads GlusterFS JSON dump
- goes through each metric read that also fits the regex filter (parameter -f)
- makes the nagios output "<check output message>| <performance metrics reported to nagios>"
- returns Nagios related result code (0 = OK, 1 = Warning, 2 = Critical, 3 = Unknown)
*/
int main(int argc, char** argv)
{
    int                 program_ret_val = 0;
    cli::Parser         parser(argc, argv);    
    std::ostringstream  error, 
                        buffer_error_output, // stores argument reletated error output to allow overriding CmdParse generated errors
                        buffer_output;  // stores argument related output to allow overriding CmdParse
    
    // create a JSON array
    //json j1 = {"one", "two", 3, 4.5, false};
    
    setup_cli_parameters(parser);

    try 
    {
        // start by setting the return value to unknown in case any error happens. 
        // if the execution reaches the end, we'll then set to OK/Critical/Warning
        program_ret_val = (int) ReturnCode::Unknown;

        //read_arguments(stdin_args, g_arg_map);

        if (parser.run(buffer_output, buffer_error_output) == false)
        {
            
            if (parser.is_set("-V"))
            {
                std::cout << PROGRAM_NAME << " " << PROGRAM_VERSION << std::endl;
                return 0;
            } else if (parser.is_set("-h")) // if this is true, the parse.run has already outputted the help to buffer_output
            {
                std::cout << buffer_output.str();
                return program_ret_val;
            }
            else 
            {
                std::cout << buffer_output.str();
                std::cout << buffer_error_output.str();
                throw std::invalid_argument("Passed arguments are invalid.");
            }
        } 
    
        
        // STDIN arguments, the g_ are because these used to be globals, ToDO: remove the g_'s
        double      g_warning           = parser.get<double>("w");
        double      g_critical          = parser.get<double>("c");
        std::string g_volname           = parser.get<std::string>("vol");
        UnitType    g_unit_type_input   = map_enum_to_value<UnitType>(g_unit_enum_map, parser.get<std::string>("u"));
        UnitType    g_unit_type_output  = map_enum_to_value<UnitType>(g_unit_enum_map, parser.get<std::string>("ou"));
        UnitType    g_gluster_unit_type = map_enum_to_value<UnitType>(g_unit_enum_map, parser.get<std::string>("gluster-src-unit"));
        std::string g_filter_regex      = parser.get<std::string>("f");
        std::string g_gluster_stats_file= parser.get<std::string>("override-stats-file");
                    g_verbose           = parser.get<bool>("v");
        int         g_max_file_age      = parser.get<int>("dump-max-age-seconds");
        int         g_max_report_metrics= parser.get<int>("exceeded-metrics-report-count");
        bool        g_apply_on_total    = parser.get<bool>("apply-on-total-avg"); // if set to true, the total average of all metrics is compared to the thresholds

        if (g_warning > g_critical)
        {
            throw std::invalid_argument("Warning threshold has to be lower than Critical.");
        }

        if (g_gluster_stats_file == "")
        {
            g_gluster_stats_file = "/var/lib/glusterd/stats/glusterfs_" + g_volname + ".dump";
        }

        if (g_verbose)
        {
            std::cout << "Established dump file: " << g_gluster_stats_file << std::endl << "Reading timestamp..." << std::endl;
        }

        std::time_t stats_last_modified = get_file_timestamp(g_gluster_stats_file); // this throws
        std::time_t now                 = std::time(nullptr);

        if ((now - stats_last_modified) > g_max_file_age)
        {            
            error << "Stats dump is older than " << g_max_file_age << " seconds. Current age: " << (now - stats_last_modified) << " seconds.";
            throw check_error(error.str());
       }

        
        //read_json_dump(g_gluster_stats_file, dump_data);

        if (g_verbose) std::cout << "Reading JSON data from dump file...";

        std::vector<json>   dump_json;
        read_json_dump(g_gluster_stats_file, dump_json);

        if (dump_json.size() == 0) // if we have read any data
        {
            error << "No data was read from the dump file at " << g_gluster_stats_file;
            throw std::runtime_error(error.str());
        }

        if (g_verbose) std::cout << " Done." << std::endl;

        Metric                          warning_threshold   = {g_warning, g_unit_type_input},
                                        critical_threshold  = {g_critical, g_unit_type_input};
        std::ostringstream              output;
        std::map<std::string, Metric>   exceeding_metrics, // all metrics exceeding thresholds are placed here to form the output message at the end
                                        performance_metrics; // the metrics found and parsed in the GlusterFS dump, they are reported to Nagios as performance fields
        std::regex                      metric_filter(g_filter_regex, std::regex::ECMAScript|std::regex::icase);
        Metric                          total_average;

        if (g_verbose) std::cout << "Processing metrics..." << std::endl;

        if (g_verbose && g_filter_regex != ".*") std::cout << "Applying regex filter: " << g_filter_regex << std::endl;

        ReturnCode check_code = process_metrics(
                                    exceeding_metrics, // the function places the metrics that exceed thresholds in this map
                                    performance_metrics, // all metrics are place here by the function
                                    total_average,
                                    dump_json, // this is what was read from the GlusterFS dump file
                                    warning_threshold, 
                                    critical_threshold, 
                                    g_unit_type_output, // the type of unit used to output to performance_metrics above
                                    g_gluster_unit_type, // the type of unit as interpreted from GlusterFS dump
                                    metric_filter, // only metrics that match this regex filter are considered
                                    g_apply_on_total // if true, the function won't compare metrics with the thresholds and will always return ReturnCode::OK
                                );

        // also report the total average
        performance_metrics["total_average"] = total_average;

    
        if (g_apply_on_total)
        {
            Metric temp_warning = convert(warning_threshold, total_average.unit);
            Metric temp_critical = convert(critical_threshold, total_average.unit);

            if (total_average.value >= temp_warning.value)
            {
                check_code = ReturnCode::Warning;
                exceeding_metrics["total_average"] = total_average;
            }

            if (total_average.value >= temp_critical.value)
            {
                check_code = ReturnCode::Critical;
                exceeding_metrics["total_average"] = total_average;
            }
        }
        
        if (g_verbose) std::cout << "Processing metrics done." << std::endl;
        
        switch (check_code)
        {
            case ReturnCode::OK:
                output << "GlusterFS Latency OK - All performance metrics within thresholds. Total avg: " << total_average.value << g_unit_enum_map_reverse[total_average.unit];
                break;
            case ReturnCode::Critical:
                output << "GlusterFS Latency CRITICAL - Metrics(s) exceeding thresholds: ";
            break;
            case ReturnCode::Warning:
                output << "GlusterFS Latency WARNING - Metric(s) exceeding thresholds: ";
                break;
        }

        // if process_metrics found something exceeding, list those exceeding metrics
        if (check_code != ReturnCode::OK) 
        {
            auto metric_pair = exceeding_metrics.begin();

            // the metric_count is not used as an index here but to keep count of how many metrics we're outputing from the total number we're allowed as specified by the user
            for (int metric_count = 1; metric_pair != exceeding_metrics.end() && metric_count <= g_max_report_metrics; metric_count++)
            {
                output << metric_pair->first << ": " << metric_pair->second.value << g_unit_enum_map_reverse[metric_pair->second.unit];            

                if (std::next(metric_pair) != exceeding_metrics.end()) // if we're not at the last element
                {
                    output << ", ";
                }

                metric_pair++; // iterator
            }

            // if we did not show all the metrics, inform the user that there are more
            if ((int) exceeding_metrics.size()-g_max_report_metrics > 0)
            {
                output << " - " << exceeding_metrics.size()-g_max_report_metrics << " metrics hidden.";
            }
        }

        // dump all output
        std::cout << output.str() << "|" << nagios_output_metrics(performance_metrics, warning_threshold, critical_threshold); // outputs Nagios check friendly performance metrics
        
        program_ret_val = (int) check_code;
    } 
    catch (const check_error& ce)
    {
        std::cout << "CRITICAL - " << ce.what();

        return (int) ReturnCode::Critical;
    }
    catch (const std::runtime_error& re)
    {
        std::cout << "Runtime error: " << re.what() << std::endl;

        return (int) ReturnCode::Unknown;
    }
    catch (const std::invalid_argument& ia)
    {   
        std::cout << "Invalid paramteres/arguments: " << ia.what() << std::endl;
        return (int) ReturnCode::Unknown;        
    }
    catch (const std::logic_error& le)
    {
        std::cout << "Program logic exception: " << le.what() << std::endl;
        return (int) ReturnCode::Unknown;        
    } 
    catch (const std::exception& e)
    {
        std::cout << "Unexpected program exception: " << e.what() << std::endl;
        return (int) ReturnCode::Unknown;        
    } catch (...)
    {
        std::cout << "Unknown exception occured." << std::endl;

        return (int) ReturnCode::Unknown;
    }

    return program_ret_val;
}


void setup_cli_parameters(cli::Parser& parser)
{    
    parser.set_required<double>("w", "warning", "Warning threshold in -u units or in microseconds if -u is not specified.");
    parser.set_required<double>("c", "critical", "Critical threshold in -u units or in microseconds if -u is not specified.");
    parser.set_required<std::string>("vol", "volume", "GlusterFS Volume name.");
    parser.set_optional<std::string>("u", "unit", "us", "Time measurement unit used to interpret input arguments -w and -c. Possible values: 'us': microseconds, 'ms': miliseconds, 's': seconds");
    parser.set_optional<std::string>("ou", "out-unit", "us", "Time measurement unit used to output the key performance indicators read. Possible values: 'us': microseconds, 'ms': miliseconds, 's': seconds");
    parser.set_optional<std::string>("f", "filter", ".*usec", "Regular expression (ECMAScript grammar, case insesitive) filter. If given, only the metrics that fully match the pattern will be considered for evaluation and reporting.");    
    parser.set_optional<bool>("apply-on-total-avg", "", false, "If set to true, the thresholds are applied to the total average of all metrics instead of each metric.");
    parser.set_optional<bool>("v", "verbose", false, "Verbose output.");    
    parser.set_optional<std::string>("override-stats-file", "", "", "If given, this file will be read instead of the default GlusterFS dump file.");    
    parser.set_optional<std::string>("gluster-src-unit", "", "us", "The time unit dumped by GlusterFS.");
    parser.set_optional<int>("dump-max-age-seconds", "", 300, "Maximum dump age allowed. If the file is older, a CRITICAL will be reported.");
    parser.set_optional<int>("exceeded-metrics-report-count", "", 1000, "The maximum number of metrics to report over the threshold. Only affects check output, not performance data.");
    parser.set_optional<bool>("V", "version", false, "Show program version.");
    

    //dump-max-age-seconds
    //
}

/*
Finds 'value' in 'map' and returns the leaf.
Throws std::invalid_argument if nothing's found
*/
template <typename T> 
T map_enum_to_value(const std::map<std::string, T>& map, const std::string& value) throw (std::invalid_argument)
{
    auto elm = map.find(value); 

    if (elm != map.end())
    {
        return elm->second;
    } else
    {
        std::ostringstream error;
        error << "Invalid value given. Got '" << value << "'. Expected: ";

        for (auto map_it = map.begin(); map_it != map.end(); map_it++)
        {
            error << map_it->first << " ";
        }
        
        throw std::invalid_argument(error.str());
    }
}

ReturnCode  process_metrics(
        std::map<std::string, Metric>& exceeding_metrics, 
        std::map<std::string, Metric>& performance_metrics, 
        Metric& total_average,
        const std::vector<json>& dump_data, 
        const Metric& warning_threshold, 
        const Metric& critical_threshold, 
        const UnitType& unit_type_output,
        const UnitType& gluster_unit_type,
        const std::regex& metric_filter,
        bool disable_threshold_comparison) throw(std::exception, std::runtime_error)
{

    Metric              dump_metric;
    double              avg_sum = 0;
    ReturnCode          check_code = ReturnCode::OK;    
    std::string         buffer;

    

    for (const json& dump_json_object : dump_data)
    {
        // loop over the JSON parsed GlusterFS dump
        for (auto res_it = dump_json_object.begin(); res_it != dump_json_object.end(); res_it++)
        {
            buffer = res_it.value();


            // if the filter does not match the name of the current metric
            if (std::regex_match((std::string) res_it.key(), metric_filter) != true)
            {
                if (g_verbose) std::cout << "Skipping metric '" << res_it.key() << "', does not match regex." << std::endl;
                continue; // skip to the next metric    
            }



            //std::cout << res_it.key() << ": " << res_it.value() << std::endl; 
            try
            {
                

                // takes the JSON dump parsed metric, coverts it to specified unit type and makes the Metric object dump_metric
                dump_metric = convert({std::stod(buffer), gluster_unit_type}, warning_threshold.unit); 

                // this stores all metrics regardless of their value
                performance_metrics[res_it.key()] = convert(dump_metric, unit_type_output); // also, convert the metric to the requested output unit type (ms/s/us)

                if (g_verbose) std::cout << res_it.key() << ": " << dump_metric.value << g_unit_enum_map_reverse[dump_metric.unit];  


                if (! disable_threshold_comparison )
                {
                    if (dump_metric.value >= warning_threshold.value) 
                    {
                        check_code = ReturnCode::Warning;
                        exceeding_metrics[res_it.key()] = convert(dump_metric, unit_type_output);

                        if (g_verbose) std::cout << " - Found bigger than WARNING threhsold! Threshold: " << warning_threshold.value << g_unit_enum_map_reverse[warning_threshold.unit];
                    }

                    if (dump_metric.value >= critical_threshold.value)
                    {
                        check_code = ReturnCode::Critical;
                        exceeding_metrics[res_it.key()] = convert(dump_metric, unit_type_output);

                        if (g_verbose) std::cout << " - Found bigger than CRITICAL threhsold! Threshold: " << critical_threshold.value << g_unit_enum_map_reverse[critical_threshold.unit];
                    }       
                }                                
                    

                if (dump_metric.value != 0.0)
                {
                    Metric output_type = convert(dump_metric, unit_type_output);
                    avg_sum += output_type.value;
                }
                                
            } 
            catch (const std::exception& e)
            {
                std::ostringstream osserror;
                osserror << "Error reading GlusterFS dump. Found the value of '" << res_it.value() <<"' for the metric '" << res_it.key()  << "' which failed when passed to std::stod.";
                throw std::runtime_error(osserror.str());
            }

             if (g_verbose) std::cout << std::endl;

        } // end for (auto res_it = dump_json_object.begin(); res_it != dump_json_object.end(); res_it++)

       

    } // end for (const json& dump_json_object : dump_data)  

    if (avg_sum != 0 && performance_metrics.size() != 0)
    {
        total_average.value = avg_sum / performance_metrics.size();
    } else
        total_average.value = 0;

    total_average.unit = unit_type_output;

    return check_code;
}


/* 
As a result of the JSON library not supporting multiple root objects within the same parsing input,
this function parses the top level JSON objects by their enclosing {}'s separating each root level
object into different inputs sent to the JSON library.

This is needed because GlusterFS 3.8 dumps two root level objects in the stats.
*/
void read_json_dump(const std::string& file_path, std::vector<json>& results) throw (std::runtime_error)
{
    
    std::ostringstream  error_message,
                        current_object_buffer;
    std::ifstream       json_file(file_path);
    std::string         current_line;
    int                 level  = -1; // -1 means no object found    
    int                 line_count = 1;

    if (! json_file.is_open())
    {        
        error_message << "Couldn't open file '" << file_path << "'";
        throw std::runtime_error(error_message.str());
    }


    while(std::getline(json_file, current_line))
    {
        current_object_buffer << current_line;

        for (int current_character_pos = 0; current_character_pos < current_line.length(); current_character_pos++)
        {
            switch(current_line[current_character_pos])
            {
                case '{':                
                    level > 0 ? level++ : level = 1;
                    
                    break;
                case '}':

                    if (level > 0)
                        level--;
                    else 
                    {
                        error_message << "Unexpected '}' found at line" << line_count << ":" << current_character_pos;
                        throw std::runtime_error(error_message.str());
                    }

                    break;
                case '[':
                    if (level == -1 && results.size() == 0) // if we're not inside any objects, the file MIGHT be enclosed in arrays
                    {
                        // let's pass this to the library and let it throw should it be the case
                        json_file.seekg(0);
                        results.push_back(json::parse(json_file));
                    }
            } 

            if  (level == 0)           
            {                                
                results.push_back(json::parse(current_object_buffer.str()));
            
                // reset the state machine
                level = -1;
                current_object_buffer.clear();
                current_object_buffer.seekp(0);
            }
        }

        line_count++;
    }

}

std::time_t get_file_timestamp(std::string& path) throw (std::runtime_error)
{
    struct stat attrib;
    int         stat_ret = 0;
    int         err_code;

    stat_ret = stat(path.c_str(), &attrib); //syscall
    err_code = errno; // I can't believe they (UNIX people?) defined a global variable where they put the error code

    if (stat_ret == -1)
    {
        std::ostringstream error;
        error << strerror(err_code) << " While trying to open: " << path;

        throw std::runtime_error(error.str());
    }

    return attrib.st_mtim.tv_sec; // return the seconds
}

std::string nagios_output_metrics(const std::map<std::string, Metric>& metrics, const Metric& warn, const Metric& crit)
{
    std::ostringstream output;
    auto& unit_map = g_unit_enum_map_reverse;

    Metric warn_t, crit_t;

    for (auto& metric : metrics)
    {
        warn_t = convert(warn, metric.second.unit);
        crit_t = convert(crit, metric.second.unit);
        output << '\'' << metric.first << "'=" << metric.second.value << unit_map[metric.second.unit] << ";" << warn_t.value <<  ";" << crit_t.value << " ";
    }

    return output.str();
}


Metric convert (const Metric& src, const UnitType dst_unit)
{
    std::map<UnitType, int> exp_map = {
        {UnitType::Microseconds, 6},
        {UnitType::Miliseconds,  3},
        {UnitType::Seconds,      0}
    };

    double  result      = 0.0;
    int     exp_diff    = exp_map[src.unit] - exp_map[dst_unit];

    if (exp_diff < 0)
    {
        result = src.value * std::pow(10, std::abs(exp_diff));
    } 
    else if (exp_diff > 0)
    {
        result = src.value / std::pow(10, std::abs(exp_diff));
    } 
    else
    {
        result = src.value;
    }

    return {result, dst_unit};
}
