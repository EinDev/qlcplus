/*
  Q Light Controller Plus - Unit tests
  avolitesd4parser_test.h

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

#ifndef AVOLITESD4PARSER_TEST_H
#define AVOLITESD4PARSER_TEST_H

#include <QObject>
#include <QTemporaryDir>

class AvolitesD4Parser_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // loadXML() error paths
    void emptyPath();
    void missingFile();
    void nonXmlFile();
    void wrongRootElement();
    void missingRequiredAttributes();

    // loadXML() happy paths
    void channels();
    void modes();
    void unknownTopLevelTags();
    void overlongChannelOffset();
    void namelessMode();
    void guessType_data();
    void guessType();

    // private helpers
    void is16Bit();
    void getCapability();
    void stringToAttributeEnum();
    void getGroup();
    void getColour();
    void comparePhysical();
    void tagGuards();

private:
    /** Write $content into a file under the temporary dir, return its path */
    QString writeDocument(const QString& name, const QString& content);

    /** Wrap $controlBody into a minimal, valid D4 document */
    static QString fixtureWithControl(const QString& controlBody);

private:
    QTemporaryDir m_dir;
};

#endif
