/*
  Q Light Controller Plus - Unit test
  functionpathutils_test.h

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

#ifndef FUNCTIONPATHUTILS_TEST_H
#define FUNCTIONPATHUTILS_TEST_H

#include <QObject>

class FunctionPathUtils_Test : public QObject
{
    Q_OBJECT

private slots:
    void membershipIsByFullPath();
    void membershipAtRoot();
    void descendantExcludesTheFolderItself();
    void rebaseRenamesNestedFolder();
    void rebaseLeavesUnrelatedPathsAlone();
    void rebaseToRootDropsTheSeparator();
    void rebaseFromRootPrefixesEverything();
    void segments();
    void moveIntoOwnSubtreeIsRefused();
    void moveToRoot();
    void moveIntoFolder();
    void renamedFolderPath();
    void notationConversion();
};

#endif
