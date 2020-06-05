/* This file is part of Flowee
 *
 * Copyright (C) 2017 Nathan Osman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * For the full copy of the License see <http://www.gnu.org/licenses/>
 */

#include <QCoreApplication>
#include <QDir>

#if defined(Q_OS_UNIX)
#  include <sys/stat.h>
#elif defined(Q_OS_WIN32)
#  include <aclapi.h>
#  include <windows.h>
#endif

#include "localfile.h"

#include "localfile_p.h"

using namespace HttpEngine;

LocalFilePrivate::LocalFilePrivate(LocalFile *localFile)
    : QObject(localFile),
      q(localFile)
{
    // Store the file in the user's home directory and set the filename to the
    // name of the application with a "." prepended
    q->setFileName(QDir::home().absoluteFilePath("." + QCoreApplication::applicationName()));
}

bool LocalFilePrivate::setPermission()
{
#if defined(Q_OS_UNIX)
    return chmod(q->fileName().toUtf8().constData(), S_IRUSR | S_IWUSR) == 0;
#elif defined(Q_OS_WIN32)
    // Windows uses ACLs to control file access - each file contains an ACL
    // which consists of one or more ACEs (access control entries) - so the
    // ACL for the file must contain only a single ACE, granting access to the
    // file owner (the current user)

    EXPLICIT_ACCESS_W ea;
    ZeroMemory(&ea, sizeof(EXPLICIT_ACCESS_W));
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea.Trustee.ptstrName = L"CURRENT_USER";

    // Create a new ACL with a single access control entry
    PACL pACL;
    if (SetEntriesInAclW(1, &ea, NULL, &pACL) != ERROR_SUCCESS) {
        return false;
    }

    // Apply the ACL to the file
    if (SetNamedSecurityInfoW((LPWSTR)q->fileName().utf16(),
                             SE_FILE_OBJECT,
                             DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                             NULL,
                             NULL,
                             pACL,
                             NULL) != ERROR_SUCCESS) {
        LocalFree(pACL);
        return false;
    }

    LocalFree(pACL);
    return true;
#else
    // Unsupported platform, so setPermission() must fail
    return false;
#endif
}

bool LocalFilePrivate::setHidden()
{
#if defined(Q_OS_UNIX)
    // On Unix, anything beginning with a "." is hidden
    return true;
#elif defined(Q_OS_WIN32)
    return SetFileAttributesW((LPCWSTR)q->fileName().utf16(), FILE_ATTRIBUTE_HIDDEN) != 0;
#else
    // Unsupported platform, so setHidden() must fail
    return false;
#endif
}

LocalFile::LocalFile(QObject *parent)
    : QFile(parent),
      d(new LocalFilePrivate(this))
{
}

bool LocalFile::openLocalFile()
{
    return QFile::open(QIODevice::WriteOnly) && d->setPermission() && d->setHidden();
}
