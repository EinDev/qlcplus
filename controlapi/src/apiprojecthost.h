/*
  Q Light Controller Plus - Control API
  apiprojecthost.h

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

#ifndef APIPROJECTHOST_H
#define APIPROJECTHOST_H

#include <QString>
#include <QStringList>

class QByteArray;

/** QSettings key for the UI locale - shared between qmlui (App::setLanguage(),
 *  main.cpp's startup language load) and ApiCoreDomain's core.settings.*
 *  handlers, hence living here rather than in qmlui/app.h. */
#define SETTINGS_LANGUAGE "ui/language"

/**
 * Project-lifecycle operations (new/open/save workspace, recent files,
 * working path) that ApiCoreDomain's core.project.* / core.settings.*
 * handlers need. qmlui's App is the only real implementation, but
 * controlapi must build and run without qmlui (see apiserver.h) - so
 * ApiCoreDomain depends on this plain interface instead of App directly,
 * obtained via dynamic_cast on ApiServer's parent (see
 * ApiCoreDomain::projectHost()).
 */
class ApiProjectHost
{
public:
    virtual ~ApiProjectHost() {}

    virtual QString fileName() const = 0;
    virtual void setFileName(const QString &fileName) = 0;

    virtual bool newWorkspace() = 0;
    virtual bool loadWorkspace(const QString &fileName) = 0;
    virtual bool saveWorkspace(const QString &fileName) = 0;
    virtual void slotLoadDocFromMemory(QByteArray &xmlData) = 0;

    virtual QStringList recentFiles() const = 0;

    virtual QString workingPath() const = 0;
    virtual void setWorkingPath(QString workingPath) = 0;
};

#endif
