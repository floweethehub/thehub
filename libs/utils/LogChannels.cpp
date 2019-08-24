/*
 * This file is part of the Flowee project
 * Copyright (C) 2017-2019 Tom Zander <tomz@freedommail.ch>
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
#include "LogChannels_p.h"

#include <boost/filesystem.hpp>
#include <iostream>

namespace {
std::string shortenMethod(const char *methodName) {
    assert(methodName);
    const char *start = strchr(methodName, ' ');
    const char *end = strchr(methodName, '(');
    if (end) {
        if (!start || start > end)
            start = methodName;
        else
            ++start;
        ++end;
        std::string copy(start, end - start);
        return std::move(copy);
    }
    return std::string();
}

}

Log::Channel::Channel(TimeStampFormat f)
    : m_timeStampFormat(f),
      m_printSection(true),
      m_printLineNumber(false),
      m_printMethodName(true),
      m_printFilename(false),
      m_showSubSecondPrecision(true)
{
}

bool Log::Channel::printSection() const
{
    return m_printSection;
}

void Log::Channel::setPrintSection(bool printSection)
{
    m_printSection = printSection;
}

bool Log::Channel::printLineNumber() const
{
    return m_printLineNumber;
}

void Log::Channel::setPrintLineNumber(bool printLineNumber)
{
    m_printLineNumber = printLineNumber;
}

bool Log::Channel::printMethodName() const
{
    return m_printMethodName;
}

void Log::Channel::setPrintMethodName(bool printMethodName)
{
    m_printMethodName = printMethodName;
}

bool Log::Channel::printFilename() const
{
    return m_printFilename;
}

void Log::Channel::setPrintFilename(bool printFilename)
{
    m_printFilename = printFilename;
}

Log::Channel::TimeStampFormat Log::Channel::timeStampFormat() const
{
    return m_timeStampFormat;
}

void Log::Channel::setTimeStampFormat(const TimeStampFormat &timeStampFormat)
{
    m_timeStampFormat = timeStampFormat;
}

bool Log::Channel::showSubSecondPrecision() const
{
    return m_showSubSecondPrecision;
}

void Log::Channel::setShowSubSecondPrecision(bool showSubSecondPrecision)
{
    m_showSubSecondPrecision = showSubSecondPrecision;
}


// ------------------------------------------------------

ConsoleLogChannel::ConsoleLogChannel()
    : Channel(TimeOnly)
{
}

void ConsoleLogChannel::setPrefix(const char *prefix)
{
    m_prefix = prefix;
}

void ConsoleLogChannel::pushLog(int64_t, std::string *timestamp, const std::string &line, const char *filename, int lineNumber, const char *methodName, short logSection, short logLevel)
{
    std::ostream &out = (logLevel == Log::WarningLevel || logLevel == Log::FatalLevel) ? std::clog : std::cout;
    if (timestamp)
        out << *timestamp << ' ';
    if (m_printSection && logSection) {
        out << '[';
        const std::string section = Log::Manager::sectionString(logSection);
        if (!section.empty())
            out << section;
        else
            out << logSection;
        out << "] ";
    }
    if (m_prefix)
        out << m_prefix << ' ';
    if (m_printFilename && filename)
        out << filename << (m_printLineNumber ? ':' : ' ');
    if (m_printLineNumber && lineNumber)
        out << lineNumber << ';';
    if (m_printMethodName && methodName) {
        std::string m(shortenMethod(methodName));
        if (!m.empty())
            out << m << ") ";
    }
    out << line;
    if (line.empty() || line.back() != '\n')
        out << std::endl;
    out.flush();
}

FileLogChannel::FileLogChannel(const boost::filesystem::path &logFilename)
    : Channel(DateTime),
      m_fileout(0),
      m_logFilename(logFilename)
{
}

FileLogChannel::~FileLogChannel()
{
    if (m_fileout)
        fclose(m_fileout);
}

static void FileWriteStr(const std::string &str, FILE *fp) {
    fwrite(str.data(), 1, str.size(), fp);
}

void FileLogChannel::pushLog(int64_t, std::string *timestamp, const std::string &line, const char*, int, const char *methodName, short logSection, short)
{
    if (m_fileout) {
        if (timestamp) {
            FileWriteStr(*timestamp, m_fileout);
            FileWriteStr(" ", m_fileout);
        }
        if (m_printSection && logSection) {
            FileWriteStr("[", m_fileout);
            const std::string section = Log::Manager::sectionString(logSection);
            if (section.empty()) {
                std::ostringstream num;
                num << logSection;
                FileWriteStr(num.str() , m_fileout);
            } else {
                FileWriteStr(section , m_fileout);
            }
            FileWriteStr("] ", m_fileout);
        }
        if (m_printMethodName && methodName) {
            std::string m(shortenMethod(methodName));
            if (!m.empty()) {
                FileWriteStr(m , m_fileout);
                FileWriteStr(") " , m_fileout);
            }
        }
        FileWriteStr(line, m_fileout);
        if (line.empty() || line.back() != '\n')
            FileWriteStr("\n", m_fileout);
    }
}

void FileLogChannel::reopenLogFiles()
{
    if (m_fileout)
        fclose(m_fileout);
    if (m_logFilename.empty())
        return;
    boost::filesystem::create_directories(m_logFilename.parent_path());

    m_fileout = fopen(m_logFilename.string().c_str(), "a");
    if (!m_fileout)
        throw std::runtime_error(std::string("Failed to open(append) file: ") + m_logFilename.string());
    setbuf(m_fileout, NULL); // unbuffered
}

void FileLogChannel::setPath(const std::string &path)
{
    if (!m_logFilename.empty() && boost::filesystem::is_directory(path))
        // interpret as dir, append previous filename.
        m_logFilename = boost::filesystem::path(path) / m_logFilename.filename();
    else
        m_logFilename = path;
}
