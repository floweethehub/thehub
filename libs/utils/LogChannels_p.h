/*
 * This file is part of the Flowee project
 * Copyright (C) 2017-2018 Tom Zander <tomz@freedommail.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LOGCHANNELS_P_H
#define LOGCHANNELS_P_H

#include "Logger.h"

#include <boost/date_time/posix_time/posix_time.hpp>

namespace Log {

class Channel {
public:
    enum TimeStampFormat {
        NoTime,
        TimeOnly,
        DateTime
    };
    Channel(TimeStampFormat formatTimestamp);
    virtual ~Channel() {}

    virtual void pushLog(int64_t timeMillis, std::string *timestamp, const std::string &line, const char *filename,
                         int lineNumber, const char *methodName, short logSection, short logLevel) = 0;

    virtual void reopenLogFiles() {}

    bool printSection() const;
    void setPrintSection(bool printSection);

    bool printLineNumber() const;
    void setPrintLineNumber(bool printLineNumber);

    bool printMethodName() const;
    void setPrintMethodName(bool printMethodName);

    bool printFilename() const;
    void setPrintFilename(bool printFilename);

    TimeStampFormat timeStampFormat() const;
    void setTimeStampFormat(const TimeStampFormat &timeStampFormat);

    bool showSubSecondPrecision() const;
    void setShowSubSecondPrecision(bool showSubSecondPrecision);

    virtual void setPath(const std::string &path) {}

protected:
    TimeStampFormat m_timeStampFormat;
    bool m_printSection;
    bool m_printLineNumber;
    bool m_printMethodName;
    bool m_printFilename;
    bool m_showSubSecondPrecision;
};

}

class ConsoleLogChannel : public Log::Channel
{
public:
    ConsoleLogChannel();

    void setPrefix(const std::string &prefix);
    const std::string &prefix() const {
        return m_prefix;
    }

    virtual void pushLog(int64_t timeMillis, std::string *timestamp, const std::string &line, const char *filename,
                         int lineNumber, const char *methodName, short logSection, short logLevel);

private:
    std::string m_prefix;
};

class FileLogChannel : public Log::Channel
{
public:
    FileLogChannel(const boost::filesystem::path &logFilename);
    ~FileLogChannel();

    virtual void pushLog(int64_t timeMillis, std::string *timestamp, const std::string &line, const char *filename,
                         int lineNumber, const char *methodName, short logSection, short logLevel);
    virtual void reopenLogFiles();

    virtual void setPath(const std::string &path);

private:
    FILE *m_fileout;
    boost::filesystem::path m_logFilename;
};

#endif
