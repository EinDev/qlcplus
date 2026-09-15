/*
  Q Light Controller Plus
  functionpathutils.cpp

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "functionpathutils.h"

bool FunctionPathUtils::isInFolder(const QString &path, const QString &folder, QChar sep)
{
    if (folder.isEmpty())
        return true;

    return path == folder || path.startsWith(folder + sep);
}

bool FunctionPathUtils::isDescendantOf(const QString &path, const QString &folder, QChar sep)
{
    if (folder.isEmpty())
        return !path.isEmpty();

    return path.startsWith(folder + sep);
}

QString FunctionPathUtils::rebase(const QString &path, const QString &oldFolder,
                                  const QString &newFolder, QChar sep)
{
    if (!isInFolder(path, oldFolder, sep))
        return path;

    if (oldFolder.isEmpty())
        return join(newFolder, path, sep);

    QString suffix = path.mid(oldFolder.length()); // "" or sep + rest
    if (newFolder.isEmpty())
    {
        // the folder is dissolved into the root: drop the leading separator
        return suffix.startsWith(sep) ? suffix.mid(1) : suffix;
    }

    return newFolder + suffix;
}

QString FunctionPathUtils::lastSegment(const QString &path, QChar sep)
{
    int idx = path.lastIndexOf(sep);
    if (idx < 0)
        return path;
    return path.mid(idx + 1);
}

QString FunctionPathUtils::parentPath(const QString &path, QChar sep)
{
    int idx = path.lastIndexOf(sep);
    if (idx < 0)
        return QString();
    return path.left(idx);
}

QString FunctionPathUtils::join(const QString &parent, const QString &name, QChar sep)
{
    if (parent.isEmpty())
        return name;
    if (name.isEmpty())
        return parent;
    return parent + sep + name;
}

bool FunctionPathUtils::isMoveIntoOwnSubtree(const QString &folder, const QString &target, QChar sep)
{
    if (folder.isEmpty() || target.isEmpty())
        return false;

    return isInFolder(target, folder, sep);
}

QString FunctionPathUtils::movedFolderPath(const QString &folder, const QString &target, QChar sep)
{
    return join(target, lastSegment(folder, sep), sep);
}

QString FunctionPathUtils::renamedFolderPath(const QString &folder, const QString &newName, QChar sep)
{
    return join(parentPath(folder, sep), newName, sep);
}

QString FunctionPathUtils::toTreePath(const QString &functionPath, QChar treeSep)
{
    QString result = functionPath;
    return result.replace(QLatin1Char('/'), treeSep);
}

QString FunctionPathUtils::toFunctionPath(const QString &treePath, QChar treeSep)
{
    QString result = treePath;
    return result.replace(treeSep, QLatin1Char('/'));
}
