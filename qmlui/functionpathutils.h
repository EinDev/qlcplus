/*
  Q Light Controller Plus
  functionpathutils.h

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

#ifndef FUNCTIONPATHUTILS_H
#define FUNCTIONPATHUTILS_H

#include <QString>

/**
 * Pure, static path arithmetic for the Function Manager's folders.
 *
 * Functions carry a "path" string (Function::path(true), '/'-separated),
 * folders are implicit tree nodes built from those paths. The tree itself
 * uses TreeModel::separator() ('`') between segments. Every helper here
 * takes the separator explicitly so the same logic serves both notations
 * and stays unit-testable without a Doc or a QQuickView.
 *
 * A folder is always identified by its FULL path: "A/X" and "X" are two
 * different folders even though they share a name, and "Foo" does not
 * contain "Foobar/...".
 */
class FunctionPathUtils
{
public:
    /** True if $path is exactly $folder or lives anywhere below it.
     *  An empty $folder is the root, which contains everything. */
    static bool isInFolder(const QString &path, const QString &folder, QChar sep);

    /** True if $path lies strictly below $folder (not equal to it). */
    static bool isDescendantOf(const QString &path, const QString &folder, QChar sep);

    /** Rewrite the $oldFolder prefix of $path into $newFolder. Paths not inside
     *  $oldFolder come back unchanged. */
    static QString rebase(const QString &path, const QString &oldFolder,
                          const QString &newFolder, QChar sep);

    /** Last segment of $path (the folder's or function's own name). */
    static QString lastSegment(const QString &path, QChar sep);

    /** Everything before the last segment; empty for a top-level path. */
    static QString parentPath(const QString &path, QChar sep);

    /** Join $parent and $name, tolerating an empty (root) parent. */
    static QString join(const QString &parent, const QString &name, QChar sep);

    /** True if moving $folder into $target must be refused because $target
     *  is the folder itself or one of its descendants. A folder can always
     *  be moved to the root (empty $target). */
    static bool isMoveIntoOwnSubtree(const QString &folder, const QString &target, QChar sep);

    /** The full path $folder gets when dropped into $target (root if empty),
     *  i.e. $target + sep + lastSegment($folder). */
    static QString movedFolderPath(const QString &folder, const QString &target, QChar sep);

    /** The full path $folder gets when renamed in place to $newName. */
    static QString renamedFolderPath(const QString &folder, const QString &newName, QChar sep);

    /** Convert between the tree notation (TreeModel::separator()) and the
     *  Function::path() notation ('/'). */
    static QString toTreePath(const QString &functionPath, QChar treeSep);
    static QString toFunctionPath(const QString &treePath, QChar treeSep);
};

#endif // FUNCTIONPATHUTILS_H
