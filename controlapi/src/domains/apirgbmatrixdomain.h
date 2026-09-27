/*
  Q Light Controller Plus - Control API
  apirgbmatrixdomain.h

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

#ifndef APIRGBMATRIXDOMAIN_H
#define APIRGBMATRIXDOMAIN_H

#include <QObject>
#include <QJsonObject>

class ApiServer;
class Doc;
class RGBMatrix;
class Function;

/**
 * docs/api-spec/fragments/functions-advanced.yaml, the functions.rgbmatrix.*
 * slice: the RGB Matrix editor's server side, mirroring what
 * qmlui/rgbmatrixeditor.cpp can set.
 *
 * - functions.rgbmatrix.listAlgorithms: the global catalog (the 4 built-in
 *   RGBAlgorithm subclasses + every RGBScript in Doc::rgbScriptsCache()),
 *   each with acceptColors() so a client shows only the colour slots the
 *   algorithm reads.
 * - functions.rgbmatrix.getScriptProperties: an RGBScript's editable
 *   RGBScriptProperty list (list/range/float/string), by script name.
 * - functions.rgbmatrix.setConfig (§4a, baseRevision): fixture group,
 *   algorithm (by name; script properties, text/font/image/animation/offset
 *   parameters), the 5 colour slots, control mode, blend mode, legacy
 *   dimmerControl. Keys absent from `config` are left unchanged - a strict
 *   superset of the spec's "whole config" shape, so a client can send one
 *   field per control. Broadcasts functions.rgbmatrix.configChanged with the
 *   full config read back from the engine.
 * - functions.rgbmatrix.setScriptProperty (§4a): one script property,
 *   validated against the loaded script's property list. Broadcasts
 *   functions.rgbmatrix.scriptPropertyChanged.
 * - functions.rgbmatrix.getPreview: one rendered frame (RGBMatrix::previewMap
 *   through a private RGBMatrixStep, so the running function's own step
 *   handler is never touched) plus stepsCount/width/height so a client can
 *   animate by polling, the way RGBMatrixEditor::slotPreviewTimeout() does
 *   in-process.
 *
 * functions.get's typeDetail for Function::RGBMatrixType
 * (FunctionsRgbMatrixDetail) is registered through
 * ApiFunctionsDomain::setTypeDetailProvider() from the constructor.
 *
 * RGBScript (rgbscriptv4.cpp) runs every script call on its own JSThread
 * via BlockingQueuedConnection, so calling it from the API server's (main)
 * thread is safe and synchronous; algorithm parameter writes take
 * RGBMatrix::algorithmMutex() exactly like the Qt editor does, because
 * MasterTimer may be reading the same algorithm on its own thread.
 */
class ApiRgbMatrixDomain : public QObject
{
    Q_OBJECT

public:
    ApiRgbMatrixDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

    /** FunctionsRgbMatrixConfig for one matrix, as functions.get and the
     *  configChanged event carry it. Exposed for the test suite. */
    static QJsonObject configToJson(Doc *doc, RGBMatrix *matrix);

private:
    void registerMethods();

    /** functions.rgbmatrix.saveToSequence - RGBMatrixEditor::saveToSequence():
     *  a hidden Scene of the group's heads plus a Sequence with one step per
     *  matrix step, the matrix's colours rendered into the control-mode channels. */
    void registerSaveToSequence();

    /** Apply the keys present in `config` to `matrix`. Returns an empty
     *  string on success, else an INVALID_PARAMS/NOT_FOUND message (the
     *  error code is returned through `code`). Nothing is applied when
     *  validation fails - every referenced name/id is checked first. */
    QString applyConfig(RGBMatrix *matrix, const QJsonObject &config, QString &code);

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
