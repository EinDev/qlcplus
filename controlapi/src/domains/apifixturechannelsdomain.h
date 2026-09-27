/*
  Q Light Controller Plus - Control API
  apifixturechannelsdomain.h

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

#ifndef APIFIXTURECHANNELSDOMAIN_H
#define APIFIXTURECHANNELSDOMAIN_H

#include <QObject>
#include <QList>

class ChannelModifier;
class ApiServer;
class Doc;

/**
 * Per-channel fixture behaviour and the libraries behind it
 * (docs/api-spec/fragments/fixtures.yaml, "Channel behaviour, modifier
 * templates, colour filters"):
 *
 * - fixtures.channel.setBehaviour: forced HTP/LTP, can-fade and the channel
 *   modifier of one channel of a patched fixture (optionally of every fixture
 *   with the same definition+mode), §4a document state. The Fixture setters
 *   never call Doc::setModified() nor touch the universes, so this domain does
 *   both (Doc::updateFixtureChannelCapabilities()).
 * - fixtures.modifiers.*: the channel modifier template library
 *   (Doc::modifiersCache(), files in QLCModifiersCache::userTemplateDirectory(),
 *   overridable with QLCPLUS_USER_MODIFIERS_DIR), a §4c library resource with
 *   the domain-local modifiersRevision.
 * - fixtures.colorFilters.list: the colour filter files the Colour tool's
 *   Filters tab offers (qmlui/colorfilters.cpp's format, read-only here).
 */
class ApiFixtureChannelsDomain : public QObject
{
    Q_OBJECT

public:
    ApiFixtureChannelsDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);
    ~ApiFixtureChannelsDomain();

private:
    void registerMethods();

private:
    Doc *m_doc;
    ApiServer *m_server;
    quint32 m_modifiersRevision;

    /** Deleted templates: universes may have read the pointer on the output
     *  thread right before they were detached, so the instances are only
     *  freed with the domain (a few hundred bytes each, deletes are rare). */
    QList<ChannelModifier *> m_retiredModifiers;
};

#endif
