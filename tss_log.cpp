//
//

#include "tss_helper.h"
#include "tss_log.h"

char *find_file_name(const char *name)
{
    int sep = '/';
    if (NULL == name) {
        return NULL;
    }

    char *name_start = (char*)strrchr(name, sep);
    return (NULL == name_start)?(char*)name:(name_start + 1);
}

char* GetLogFile (char *file_name)
{
    char* LogFile = new char[64];
    char *tss_home = getenv("TSS_HOME");
    if (tss_home != NULL)
    {
        sprintf (LogFile, "%s/log/%s.log", tss_home, file_name);
    }
    else
    {
        sprintf (LogFile, "log/%s.log", file_name);
    }
    return LogFile;
}

void InitLog (char *argv0, Logger& log)
{
    char *file_name = find_file_name (argv0);
    char* LogFile = GetLogFile (file_name);
    char* CfgFile = GetCfgFile ("log4cplus.cfg");

    if (CfgFile == NULL)
    {
        SharedAppenderPtr pFileAppender(new FileAppender((LogFile)));
        std::auto_ptr<Layout> pPatternLayout(new PatternLayout("[%p] [%l] [PID:%i] [TID:%t] [%D] - %m %n"));
        pFileAppender->setLayout(pPatternLayout);
        log = Logger::getInstance(file_name);
        log.setLogLevel(DEBUG_LOG_LEVEL);
        log.addAppender(pFileAppender);
    }
    else
    {
        PropertyConfigurator::doConfigure(CfgFile);
        log = Logger::getInstance(file_name);
    }
}
