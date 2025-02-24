/** Configure
 */
#include "configure.h"

using namespace ttx;

int Configure::DirExists(std::string *path)
{
    struct stat info;

    if(stat(path->c_str(), &info ) != 0)
        return 0;
    else if(info.st_mode & S_IFDIR)
        return 1;
    else
        return 0;
}

Configure::Configure(int argc, char** argv) :
    // settings for generation of packet 8/30
    _initialMag(1),
    _initialPage(0x00),
    _initialSubcode(0x3F7F),
    _NetworkIdentificationCode(0x0000),
    _CountryNetworkIdentificationCode(0x0000),
    _reservedBytes{0x15, 0x15, 0x15, 0x15}, // initialise reserved bytes to hamming 8/4 encoded 0
    _serviceStatusString(20, ' '),
    _subtitleRepeats(1)
{
    _configFile = CONFIGFILE;
    
#ifdef RASPBIAN
    _pageDir = "/home/pi/teletext";
#else
    _pageDir = "./pages"; // a relative path as a sensible default
#endif
    // This is where the default header template is defined.
    _headerTemplate = "VBIT2    %%# %%a %d %%b" "\x03" "%H:%M:%S";
    
    // the default command interface port
    _commandPort = 5570;
    _commandPortEnabled = false;
    
    _reverseBits = false;
    _debugLevel = 0;

    _rowAdaptive = false;
    _linesPerField = 16; // default to 16 lines per field

    _multiplexedSignalFlag = false; // using this would require changing all the line counting and a way to send full field through raspi-teletext - something for the distant future when everything else is done...
    
    _OutputFormat = T42; // t42 output is the default behaviour
    
    uint8_t priority[8]={9,3,3,6,3,3,5,6}; // 1=High priority,9=low. Note: priority[0] is mag 8
    
    for (int i=0; i<8; i++)
        _magazinePriority[i] = priority[i];

    //Scan the command line for overriding the pages file.
    if (argc>1)
    {
        for (int i=1;i<argc;++i)
        {
            std::string arg = argv[i];
            if (arg == "--dir")
            {
                if (i + 1 < argc)
                    _pageDir = argv[++i];
                else
                {
                    std::cerr << "[Configure::Configure] --dir requires an argument\n";
                    exit(EXIT_FAILURE);
                }
            }
            else if (arg == "--format")
            {
                if (i + 1 < argc)
                {
                    arg = argv[++i];
                    
                    if (arg == "t42")
                    {
                        _OutputFormat = T42;
                    }
                    else if (arg == "raw")
                    {
                        _OutputFormat = Raw;
                    }
                    else if (arg == "PES")
                    {
                        _OutputFormat = PES;
                    }
                    
                    if (_reverseBits && _OutputFormat != T42)
                    {
                        std::cerr << "[Configure::Configure] --reverse requires t42 format\n";
                        exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    std::cerr << "[Configure::Configure] --format requires an argument\n";
                    exit(EXIT_FAILURE);
                }
            }
            else if (arg == "--reverse")
            {
                _reverseBits = true;
                
                if (_OutputFormat != T42)
                {
                    std::cerr << "[Configure::Configure] --reverse requires t42 format\n";
                    exit(EXIT_FAILURE);
                }
            }
            else if (arg == "--reserved")
            {
                if (i + 1 < argc)
                {
                    // Take a 32 bit hexadecimal value to set the four reserved bytes in the BSDP
                    // Store bytes big endian so that the order digits appear on the command line is the same as they appear in packet
                    errno = 0;
                    char *end_ptr;
                    unsigned long l = std::strtoul(argv[++i], &end_ptr, 16);
                    if (errno == 0 && *end_ptr == '\0')
                    {
                        _reservedBytes[0] = (l >> 24) & 0xff;
                        _reservedBytes[1] = (l >> 16) & 0xff;
                        _reservedBytes[2] = (l >> 8) & 0xff;
                        _reservedBytes[3] = (l >> 0) & 0xff;
                    }
                    else
                    {
                        std::cerr << "[Configure::Configure] invalid reserved bytes argument\n";
                        exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    std::cerr << "[Configure::Configure] --reserved requires an argument\n";
                    exit(EXIT_FAILURE);
                }
            }
            else if (arg == "--debug")
            {
                if (i + 1 < argc)
                {
                    errno = 0;
                    char *end_ptr;
                    long l = std::strtol(argv[++i], &end_ptr, 10);
                    if (errno == 0 && *end_ptr == '\0' && l > -1 && l < MAXDEBUGLEVEL)
                    {
                        _debugLevel = (int)l;
                        
                        std::stringstream ss;
                        ss << "[Configure::Configure] debugging enabled at level " << _debugLevel << "\n";
                        std::cerr << ss.str();
                    }
                    else
                    {
                        std::cerr << "[Configure::Configure] invalid debug level argument\n";
                        exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    std::cerr << "[Configure::Configure] --debug requires an argument\n";
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    
    if (!DirExists(&_pageDir))
    {
        std::stringstream ss;
        ss << "[Configure::Configure] " << _pageDir << " does not exist or is not a directory\n";
        std::cerr << ss.str();
        exit(EXIT_FAILURE);
    }
    
    // TODO: allow overriding config file from command line
    std::stringstream ss;
    ss << "[Configure::Configure] Pages directory is " << _pageDir << "\n";
    ss << "[Configure::Configure] Config file is " << _configFile << "\n";
    std::cerr << ss.str();
    
    std::string path;
    path = _pageDir;
    path += "/";
    path += _configFile;
    LoadConfigFile(path); // load main config file (vbit.conf)
    
    LoadConfigFile(path+".override"); // allow overriding main config file for local configuration where main config is in version control
}

Configure::~Configure()
{
    std::cerr << "[Configure] Destructor\n";
}

int Configure::LoadConfigFile(std::string filename)
{
    std::ifstream filein(filename.c_str()); // Open the file

    std::vector<std::string>::iterator iter;
    // these are all the valid strings for config lines
    std::vector<std::string> nameStrings{ "header_template", "initial_teletext_page", "row_adaptive_mode", "network_identification_code", "country_network_identification", "full_field", "status_display", "subtitle_repeats","enable_command_port","command_port","lines_per_field","magazine_priority" };

    if (filein.is_open())
    {
        std::stringstream ss;
        ss << "[Configure::LoadConfigFile] opened " << filename << "\n";
        std::cerr << ss.str();

        std::string line;
        std::string name;
        std::string value;
        TTXLine* header = new TTXLine();
        while (std::getline(filein >> std::ws, line))
        {
            if (line.front() != ';') // ignore comments
            { 
                std::size_t delim = line.find("=", 0);
                int error = 0;

                if (delim != std::string::npos)
                {
                    name = line.substr(0, delim);
                    value = line.substr(delim + 1);
                    iter = find(nameStrings.begin(), nameStrings.end(), name);
                    if(iter != nameStrings.end())
                    {
                        // matched string
                        switch(iter - nameStrings.begin())
                        {
                            case 0: // header_template
                            {
                                header->Setm_textline(value,true);
                                value = header->GetLine();
                                value.resize(32,' ');
                                _headerTemplate.assign(value);
                                break;
                            }
                            case 1: // initial_teletext_page
                            {
                                if (value.size() >= 3)
                                {
                                    size_t idx;
                                    int magpage;
                                    int subcode;
                                    try
                                    {
                                        magpage = stoi(std::string(value, 0, 3), &idx, 16);
                                    }
                                    catch (const std::invalid_argument& ia)
                                    {
                                        error = 1;
                                        break;
                                    }
                                    if (magpage < 0x100 || magpage > 0x8FF || (magpage & 0xFF) == 0xFF)
                                    {
                                        error = 1;
                                        break;
                                    }
                                    if (value.size() > 3)
                                    {
                                        if ((value.size() != 8) || (value.at(idx) != ':'))
                                        {
                                            error = 1;
                                            break;
                                        }
                                        try
                                        {
                                            subcode = stoi(std::string(value, 4, 4), &idx, 16);
                                        }
                                        catch (const std::invalid_argument& ia)
                                        {
                                            error = 1;
                                            break;
                                        }
                                        if (subcode < 0x0000 || subcode > 0x3F7F || subcode & 0xC080)
                                        {
                                            error = 1;
                                            break;
                                        }
                                        _initialSubcode = subcode;
                                    }
                                    _initialMag = magpage / 0x100;
                                    _initialPage = magpage % 0x100;
                                    break;
                                }
                                error = 1;
                                break;
                            }
                            case 2: // row_adaptive_mode
                            {
                                if (!value.compare("true"))
                                {
                                    _rowAdaptive = true;
                                }
                                else if (!value.compare("false"))
                                {
                                    _rowAdaptive = false;
                                }
                                else
                                {
                                    error = 1;
                                }
                                break;
                            }
                            case 3: // "network_identification_code" - four character hex. eg. FA6F
                            {
                                if (value.size() == 4)
                                {
                                    size_t idx;
                                    try
                                    {
                                        _NetworkIdentificationCode = stoi(std::string(value, 0, 4), &idx, 16);
                                    }
                                    catch (const std::invalid_argument& ia)
                                    {
                                        error = 1;
                                        break;
                                    }
                                }
                                else
                                {
                                    error = 1;
                                }
                                break;
                            }
                            case 4: // "country_network_identification" - four character hex. eg. 2C2F
                            {
                                if (value.size() == 4)
                                {
                                    size_t idx;
                                    try
                                    {
                                        _CountryNetworkIdentificationCode = stoi(std::string(value, 0, 4), &idx, 16);
                                    }
                                    catch (const std::invalid_argument& ia)
                                    {
                                        error = 1;
                                        break;
                                    }
                                }
                                else
                                {
                                    error = 1;
                                }
                                break;
                            }
                            case 5: // "full_field"
                            {
                                break;
                            }
                            case 6: // "status_display"
                            {
                                value.resize(20,' '); // string must be 20 characters
                                _serviceStatusString.assign(value);
                                break;
                            }
                            case 7: // "subtitle_repeats" - The number of times a subtitle transmission is repeated 0..9
                            {
                                if (value.size() == 1)
                                {
                                    try
                                    {
                                        _subtitleRepeats = stoi(std::string(value, 0, 1));
                                    }
                                    catch (const std::invalid_argument& ia)
                                    {
                                        error = 1;
                                        break;
                                    }
                                }
                                else
                                {
                                    error = 1;
                                }
                                break;
                            }
                            case 8: // "enable_command_port"
                            {
                                if (!value.compare("true"))
                                {
                                    _commandPortEnabled = true;
                                }
                                else if (!value.compare("false"))
                                {
                                    _commandPortEnabled = false;
                                }
                                else
                                {
                                    error = 1;
                                }
                                break;
                            }
                            case 9: // "command_port"
                            {
                                if (value.size() > 0 && value.size() < 6)
                                {
                                    try
                                    {
                                        _commandPort = stoi(std::string(value, 0, 5));
                                    }
                                    catch (const std::invalid_argument& ia)
                                    {
                                        error = 1;
                                        break;
                                    }
                                }
                                else
                                {
                                    error = 1;
                                }
                                break;
                            }
                            case 10: // "lines_per_field"
                            {
                                if (value.size() > 0 && value.size() < 4)
                                {
                                    try
                                    {
                                        _linesPerField = stoi(std::string(value, 0, 3));
                                    }
                                    catch (const std::invalid_argument& ia)
                                    {
                                        error = 1;
                                        break;
                                    }
                                }
                                else
                                {
                                    error = 1;
                                }
                                break;
                            }
                            case 11: // "magazine_priority"
                            {
                                std::stringstream ss(value);
                                std::string temps;
                                int tmp[8];
                                int i;
                                for (i=0; i<8; i++)
                                {
                                    if (std::getline(ss, temps, ','))
                                    {
                                        try
                                        {
                                            tmp[i] = stoi(temps);
                                        }
                                        catch (const std::invalid_argument& ia)
                                        {
                                            error = 1;
                                            break;
                                        }
                                        if (!(tmp[i] > 0 && tmp[i] < 10)) // must be 1-9
                                        {
                                            error = 1;
                                            break;
                                        }
                                    }
                                    else
                                    {
                                        error = 1;
                                        break;
                                    }
                                }
                                for (i=0; i<8; i++)
                                    _magazinePriority[i] = tmp[i];
                                break;
                            }
                        }
                    }
                    else
                    {
                        error = 1; // unrecognised config line
                    }
                }
                if (error)
                {
                    std::stringstream ss;
                    ss << "[Configure::LoadConfigFile] invalid config line: " << line << "\n";
                    std::cerr << ss.str();
                }
            }
        }
        filein.close();
        return 0;
    }
    else
    {
        std::cerr << "[Configure::LoadConfigFile] open failed\n";
        return -1;
    }
}
