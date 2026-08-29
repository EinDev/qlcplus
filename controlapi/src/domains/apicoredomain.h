/*
  Q Light Controller Plus - Control API
  apicoredomain.h

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

#ifndef APICOREDOMAIN_H
#define APICOREDOMAIN_H

#include <QObject>
#include "doc.h"

class ApiServer;
class App;

/**
 * Implementation of the "core.*" domain (project lifecycle, mode, settings).
 * Deferring undo/redo per Phase 1.1 / 2.5 instructions in TODO.md.
 */
class ApiCoreDomain : public QObject
{
    Q_OBJECT

public:
    ApiCoreDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();
    App *app() const;

    /** Broadcasts core.project.loaded ({reason, project: CoreProjectMetadata}),
     *  the one event a spec-built client should refresh all its domain state
     *  from - see core.project.new/open/close handlers, the only callers. */
    void broadcastProjectLoaded(const QString &reason, const QString &originClientId);

private slots:
    void slotModeChanged(Doc::Mode mode);
    void slotDocRevisionChanged(quint32 revision);
    void slotRecentFilesChanged();
    void slotWorkingPathChanged(QString path);

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
