/*
  Q Light Controller Plus
  contextmanager.cpp

  Copyright (c) Massimo Callegari

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

#include <QQmlContext>
#include <QQuickItem>
#include <QDebug>
#include <QtMath>
#include <algorithm>

#include "contextmanager.h"
#include "monitorproperties.h"
#include "genericdmxsource.h"
#include "functionmanager.h"
#include "fixturemanager.h"
#include "qlcfixturemode.h"
#include "qlccapability.h"
#include "fixtureutils.h"
#include "showmanager.h"
#include "mainviewdmx.h"
#include "mainview2d.h"
#include "mainview3d.h"
#include "qlcchannel.h"
#include "tardis.h"
#include "app.h"
#include "doc.h"

ContextManager::ContextManager(QQuickView *view, Doc *doc,
                               FixtureManager *fxMgr,
                               FunctionManager *funcMgr,
                               QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_doc(doc)
    , m_monProps(doc->monitorProperties())
    , m_fixtureManager(fxMgr)
    , m_functionManager(funcMgr)
    , m_currentSubContext("2D")
    , m_multipleSelection(false)
    , m_positionPicking(false)
    , m_batchSelection(false)
    , m_showFixtureGroups(false)
    , m_lastPickedPoint(QVector3D())
    , m_lastClickedType(App::NoDragItem)
    , m_universeFilter(Universe::invalid())
    , m_currentFixtureGroupID(Function::invalidId())
    , m_editingEnabled(false)
    , m_selectedDimmersCount(0)
    , m_dumpChannelMask(0)
{
    m_view->rootContext()->setContextProperty("contextManager", this);

    /** Create and enable a DMX source used for dumping */
    m_source = new GenericDMXSource(m_doc);
    m_source->setOutputEnabled(true);

    /** Set the initial point of view for the 2D preview */
    m_doc->monitorProperties()->setPointOfView(MonitorProperties::TopView);

    /** Create and register the 4 contexts handled by this class */
    m_uniGridView = new PreviewContext(m_view, m_doc, "UNIGRID");
    m_uniGridView->setContextResource("qrc:/UniverseGridView.qml");
    m_uniGridView->setContextTitle(tr("Universe Grid View"));
    registerContext(m_uniGridView);

    m_DMXView = new MainViewDMX(m_view, m_doc);
    registerContext(m_DMXView);
    m_view->rootContext()->setContextProperty("ViewDMX", m_DMXView);

    m_2DView = new MainView2D(m_view, m_doc);
    registerContext(m_2DView);
    m_view->rootContext()->setContextProperty("View2D", m_2DView);

    m_3DView = new MainView3D(m_view, m_doc);
    registerContext(m_3DView);
    m_view->rootContext()->setContextProperty("View3D", m_3DView);

    qmlRegisterUncreatableType<QLCChannel>("org.qlcplus.classes", 1, 0, "QLCChannel", "Can't create a QLCChannel!");
    qmlRegisterUncreatableType<MonitorProperties>("org.qlcplus.classes", 1, 0, "MonitorProperties", "Can't create MonitorProperties!");

    connect(m_fixtureManager, &FixtureManager::newFixtureCreated, this, &ContextManager::slotNewFixtureCreated);
    connect(m_fixtureManager, &FixtureManager::fixtureDeleted, this, &ContextManager::slotFixtureDeleted);
    connect(m_fixtureManager, &FixtureManager::fixtureFlagsChanged, this, &ContextManager::slotFixtureFlagsChanged);

    connect(m_fixtureManager, &FixtureManager::channelValueChanged, this, &ContextManager::slotChannelValueChanged);
    connect(m_fixtureManager, &FixtureManager::presetChanged, this, &ContextManager::slotPresetChanged);
    connect(m_fixtureManager, &FixtureManager::itemClicked, this, &ContextManager::setLastClickedType);

    connect(m_doc->inputOutputMap(), SIGNAL(universeWritten(quint32,QByteArray)), this, SLOT(slotUniverseWritten(quint32,QByteArray)));
    connect(m_functionManager, &FunctionManager::isEditingChanged, this, &ContextManager::slotFunctionEditingChanged);
    connect(m_functionManager, &FunctionManager::itemClicked, this, &ContextManager::setLastClickedType);
}

ContextManager::~ContextManager()
{
    for (PreviewContext *context : m_contextsMap.values())
    {
        if (context->detached())
            context->deleteLater();
    }

    m_view->rootContext()->setContextProperty("contextManager", nullptr);
}

void ContextManager::registerContext(PreviewContext *context)
{
    if (context == nullptr)
        return;

    m_contextsMap[context->name()] = context;
    connect(context, &PreviewContext::keyPressed, this, &ContextManager::handleKeyPress);
    connect(context, &PreviewContext::keyReleased, this, &ContextManager::handleKeyRelease);
}

void ContextManager::unregisterContext(QString name)
{
    if (m_contextsMap.contains(name) == false)
        return;

    PreviewContext *context = m_contextsMap.take(name);

    disconnect(context, SIGNAL(keyPressed(QKeyEvent*)),
               this, SLOT(handleKeyPress(QKeyEvent*)));
    disconnect(context, SIGNAL(keyReleased(QKeyEvent*)),
               this, SLOT(handleKeyRelease(QKeyEvent*)));
}

void ContextManager::setBatchSelection(bool enable)
{
    if (m_batchSelection == enable)
        return;

    m_batchSelection = enable;

    if (m_batchSelection)
    {
        m_fixtureManager->setDeferCapabilityCounters(true);
    }
    else
    {
        // apply all the capability-counter QML updates accumulated during
        // the batch in one shot, instead of one QML property write (with
        // binding re-evaluation) per fixture that was processed
        m_fixtureManager->flushCapabilityCounters();

        emit dumpValuesCountChanged();
        emit selectedFixturesChanged();
        emit selectedDimmersCountChanged();
        emit fixturesPositionChanged();
        emit fixturesRotationChanged();
        emit fixtureDmxTransformFlagsChanged();
        emit fixtureDmxScaleChanged();

        if (m_DMXView->isEnabled())
            m_DMXView->updateFixtureSelection(m_selectedFixtures);
        if (m_2DView->isEnabled())
            m_2DView->updateFixtureSelection(m_selectedFixtures);
        if (m_3DView->isEnabled())
            m_3DView->updateFixtureSelection(m_selectedFixtures);

        if (m_selectedFixtures.isEmpty())
            m_fixtureManager->resetCapabilities();

        // force all QML trees to refresh their selection state at once
        if (m_fixtureManager->fixtureTree())
            m_fixtureManager->fixtureTree()->setItemRoleData("", 0, TreeModel::IsSelectedRole);

        refreshGroupSelectionRoles();
    }
}

bool ContextManager::isBatchSelection() const
{
    return m_batchSelection;
}

void ContextManager::refreshGroupSelectionRoles()
{
    for (FixtureGroup *group : m_doc->fixtureGroups())
    {
        if (group == nullptr)
            continue;

        m_fixtureManager->setGroupItemRoleData(group->id(), isGroupFullySelected(group->id()) ? 2 : 0,
                                                TreeModel::IsSelectedRole);
    }
}

void ContextManager::enableContext(QString name, bool enable, QQuickItem *item)
{
    if (m_contextsMap.contains(name) == false)
        return;

    PreviewContext *context = m_contextsMap[name];

    context->setContextItem(item);
    context->enableContext(enable);

    if (name == "DMX")
        m_DMXView->updateFixtureSelection(m_selectedFixtures);
    else if (name == "2D")
        m_2DView->updateFixtureSelection(m_selectedFixtures);
    else if (name == "3D")
        m_3DView->updateFixtureSelection(m_selectedFixtures);

    emit currentContextChanged();
}

PreviewContext *ContextManager::contextByName(QString ctxName)
{
    return m_contextsMap.value(ctxName, nullptr);
}

void ContextManager::detachContext(QString name)
{
    qDebug() << "[ContextManager] detaching context:" << name;
    if (m_contextsMap.contains(name) == false)
        return;

    PreviewContext *context = m_contextsMap[name];
    context->setDetached(true);
}

void ContextManager::reattachContext(QString name)
{
    qDebug() << "[ContextManager] reattaching context:" << name;
    if (m_contextsMap.contains(name) == false)
        return;

    PreviewContext *context = m_contextsMap[name];
    context->setDetached(false);

    if (name == "DMX" || name == "2D" || name == "3D" || name == "UNIGRID")
    {
        QQuickItem *viewObj = qobject_cast<QQuickItem*>(m_view->rootObject()->findChild<QObject *>("fixturesAndFunctions"));
        if (viewObj == nullptr)
            return;
        QMetaObject::invokeMethod(viewObj, "enableContext",
                Q_ARG(QVariant, name),
                Q_ARG(QVariant, false));
    }
    else if (name.startsWith("PAGE-"))
    {
        QQuickItem *viewObj = qobject_cast<QQuickItem*>(m_view->rootObject()->findChild<QObject *>("virtualConsole"));
        if (viewObj == nullptr)
            return;
        QMetaObject::invokeMethod(viewObj, "enableContext",
                Q_ARG(QVariant, name),
                Q_ARG(QVariant, false));
    }
    else
    {
        QMetaObject::invokeMethod(m_view->rootObject(), "enableContext",
                Q_ARG(QVariant, name),
                Q_ARG(QVariant, false));
    }
}

void ContextManager::switchToContext(QString name)
{
    QString ctxName = name;
    QStringList qlc4names, qlc5names;
    qlc4names << "FixtureManager" << "FunctionManager" << "ShowManager" << "VirtualConsole" << "SimpleDesk" << "InputOutputManager";
    qlc5names << "FIXANDFUNC" << "FIXANDFUNC" << "SHOWMGR" << "VC" << "SDESK" << "IOMGR";

    int ctxIndex = qlc5names.indexOf(name);
    if (ctxIndex < 0)
    {
        ctxIndex = qlc4names.indexOf(name);
        ctxName = qlc5names.at(ctxIndex < 0 ? 0 : ctxIndex);
    }

    QMetaObject::invokeMethod(m_view->rootObject(), "switchToContext",
                              Q_ARG(QVariant, ctxName),
                              Q_ARG(QVariant, QString()));
}

QString ContextManager::currentContext() const
{
    if (m_view == nullptr || m_view->rootObject() == nullptr)
        return "";

    return m_view->rootObject()->property("currentContext").toString();
}

QString ContextManager::currentSubContext() const
{
    return m_currentSubContext;
}

void ContextManager::setCurrentSubContext(QString ctx)
{
    if (ctx == m_currentSubContext)
        return;

    m_currentSubContext = ctx;
    emit currentSubContextChanged();
}

MainView2D *ContextManager::get2DView()
{
    return m_2DView;
}

MainView3D *ContextManager::get3DView()
{
    return m_3DView;
}

QVector3D ContextManager::environmentSize() const
{
    return m_monProps->gridSize();
}

void ContextManager::setEnvironmentSize(QVector3D environmentSize)
{
    if (environmentSize == m_monProps->gridSize())
        return;

    Tardis::instance()->enqueueAction(Tardis::EnvironmentSetSize, 0, m_monProps->gridSize(), environmentSize);

    m_monProps->setGridSize(environmentSize);
    if (m_2DView->isEnabled())
        m_2DView->setGridSize(environmentSize);
    if (m_3DView->isEnabled())
    {
        for (Fixture *fixture : m_doc->fixtures()) // C++11
        {
            for (quint32 &subID : m_monProps->fixtureIDList(fixture->id()))
            {
                quint16 headIndex = m_monProps->fixtureHeadIndex(subID);
                quint16 linkedIndex = m_monProps->fixtureLinkedIndex(subID);
                quint32 itemID = FixtureUtils::fixtureItemID(fixture->id(), headIndex, linkedIndex);
                m_3DView->updateFixturePosition(itemID, m_monProps->fixturePosition(fixture->id(), headIndex, linkedIndex));
            }
        }
    }
    emit environmentSizeChanged();
}

bool ContextManager::multipleSelection() const
{
    return m_multipleSelection;
}

void ContextManager::setMultipleSelection(bool multipleSelection)
{
    if (m_multipleSelection == multipleSelection)
        return;

    m_multipleSelection = multipleSelection;
    emit multipleSelectionChanged();
}



bool ContextManager::positionPicking() const
{
    return m_positionPicking;
}

void ContextManager::setPositionPicking(bool enable)
{
    if (enable == m_positionPicking)
        return;

    m_positionPicking = enable;

    emit positionPickingChanged();
}

QVector3D ContextManager::lastPickedPoint() const
{
    return m_lastPickedPoint;
}

void ContextManager::setPositionPickPoint(QVector3D point)
{
    if (positionPicking() == false)
        return;

    point = QVector3D(point.x() + m_monProps->gridSize().x() / 2,
                      point.y(),
                      point.z() + m_monProps->gridSize().z() / 2);

    m_lastPickedPoint = point;
    emit lastPickedPointChanged();

    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);

        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture == nullptr)
            continue;

        quint32 panMSB = fixture->channelNumber(QLCChannel::Pan, QLCChannel::MSB);
        quint32 tiltMSB = fixture->channelNumber(QLCChannel::Tilt, QLCChannel::MSB);
        int linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        int headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint32 itemFlags = m_monProps->fixtureFlags(fxID, headIndex, linkedIndex);

        // don't even bother if the fixture doesn't have PAN/TILT channels
        if (panMSB == QLCChannel::invalid() && tiltMSB == QLCChannel::invalid())
            continue;

        QVector3D lightPos;
        QMatrix4x4 lightMatrix;
        if (FixtureUtils::lightProperties(m_monProps, fixture, headIndex, lightPos, lightMatrix) == false)
        {
            lightPos = m_3DView->lightPosition(itemID);
            lightMatrix = m_3DView->lightMatrix(itemID);
        }

        lightPos = QVector3D(lightPos.x() + m_monProps->gridSize().x() / 2,
                             lightPos.y(),
                             lightPos.z() + m_monProps->gridSize().z() / 2);

        qDebug() << "3D point picked:" << point << "light position:" << lightPos;

        if (panMSB != QLCChannel::invalid())
        {
            QVector3D dir = (point - lightPos).normalized();

            // rotate x-axis according to light matrix.
            QVector4D res = lightMatrix * QVector4D(1.0, 0.0, 0.0, 0.0);
            QVector3D xa = QVector3D(res.x(), res.y(), res.z());

            // rotate z-axis according to light matrix.
            res = lightMatrix * QVector4D(0.0, 0.0, 1.0, 0.0);
            QVector3D za = QVector3D(res.x(), res.y(), res.z());

            QVector3D projDirX = QVector3D::dotProduct(dir, xa) * xa;
            QVector3D projDirZ = QVector3D::dotProduct(dir, za) * za;

            qreal b = projDirX.length();
            qreal c = projDirZ.length();
            qreal panDeg = qRadiansToDegrees(M_PI_2 - qAtan(c / b)); // PI/2 - angle

            bool xLeft = QVector3D::dotProduct(projDirX, xa) < 0.0 ? true : false;
            bool zBack = QVector3D::dotProduct(projDirZ, za) < 0.0 ? true : false;

            if (xLeft && !zBack)
                panDeg = 90.0 + (90.0 - panDeg);
            else if (!xLeft && !zBack)
                panDeg = 180.0 + panDeg;
            else if (!xLeft && zBack)
                panDeg = 270.0 + (90.0 - panDeg);

            if (itemFlags & MonitorProperties::InvertedPanFlag)
            {
                QLCPhysical phy = fixture->fixtureMode()->physical();
                double maxPanDeg = phy.focusPanMax() ? phy.focusPanMax() : 360;
                panDeg = maxPanDeg - panDeg;
            }

            qDebug() << "Fixture" << fxID << "pan degrees:" << panDeg;

            QList<SceneValue> svList = fixture->positionToValues(QLCChannel::Pan, panDeg);
            for (SceneValue &posSv : svList)
            {
                if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
                    setDumpValue(posSv.fxi, posSv.channel, posSv.value);
                else
                    m_functionManager->setChannelValue(posSv.fxi, posSv.channel, posSv.value);
            }
        }

        if (tiltMSB != QLCChannel::invalid())
        {
            QVector3D dir = (point - lightPos).normalized();
            // rotate y-axis according to light matrix.
            QVector4D res = lightMatrix * QVector4D(0.0, -1.0, 0.0, 0.0);
            QVector3D ya = QVector3D(res.x(), res.y(), res.z());

            qreal tiltDeg =  qRadiansToDegrees(qAcos(QVector3D::dotProduct(dir, ya)));
            QLCPhysical phy = fixture->fixtureMode()->physical();

            // clamp the tilt.
            if (tiltDeg < 0.0)
                tiltDeg = 0.0;

            if (tiltDeg > phy.focusTiltMax() / 2)
                tiltDeg = phy.focusTiltMax() / 2;

            if (itemFlags & MonitorProperties::InvertedTiltFlag)
                tiltDeg = phy.focusTiltMax() / 2 + tiltDeg;
            else
                tiltDeg = phy.focusTiltMax() / 2 - tiltDeg;

            qDebug() << "Fixture" << fxID << "tilt degrees:" << tiltDeg;

            QList<SceneValue> svList = fixture->positionToValues(QLCChannel::Tilt, tiltDeg);
            for (SceneValue &posSv : svList)
            {
                if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
                    setDumpValue(posSv.fxi, posSv.channel, posSv.value);
                else
                    m_functionManager->setChannelValue(posSv.fxi, posSv.channel, posSv.value);
            }
        }
    }

    setPositionPicking(false);
}

int ContextManager::lastClickedType() const
{
    return m_lastClickedType;
}

void ContextManager::setLastClickedType(const int &newLastClickedType)
{
    m_lastClickedType = newLastClickedType;
}

bool ContextManager::showFixtureGroups() const
{
    return m_showFixtureGroups;
}

void ContextManager::setShowFixtureGroups(bool show)
{
    if (m_showFixtureGroups == show)
        return;

    m_showFixtureGroups = show;

    emit showFixtureGroupsChanged();
}

void ContextManager::resetContexts()
{
    m_channelsMap.clear();
    resetDumpValues();

    // Loaded projects renumber Fixture IDs from 0, so a stale cache entry
    // keyed by an ID reused by the new project's fixtures would otherwise
    // resume a drag from the previous project's delta. See cachedPositionDelta()/
    // cachedRotationDelta() - both reseed on demand from the fixture's actual
    // channel values, so clearing here is safe.
    m_fixturePositionDeltaCache.clear();
    m_fixtureRotationDeltaCache.clear();

    for (quint32 &itemID : m_selectedFixtures)
        setFixtureSelection(itemID, -1, false);
    m_selectedFixtures.clear();

    m_functionManager->setEditorFunction(-1, true, false);
    m_functionManager->selectFunctionID(-1, false);
    m_editingEnabled = false;

    emit environmentSizeChanged();

    if (m_DMXView->isEnabled())
        m_DMXView->slotRefreshView();
    if (m_2DView->isEnabled())
        m_2DView->slotRefreshView();
    if (m_3DView->isEnabled())
        m_3DView->slotRefreshView();

    /** TODO: nothing to do on the other contexts ? */
}

void ContextManager::resetViewItems()
{
    m_channelsMap.clear();

    // iterate on a copy: setFixtureSelection() removes entries from m_selectedFixtures
    const QList<quint32> selected = m_selectedFixtures;
    for (const quint32 &itemID : selected)
        setFixtureSelection(itemID, -1, false);
    m_selectedFixtures.clear();

    if (m_2DView->isEnabled())
        m_2DView->resetItems();
    if (m_3DView->isEnabled())
        m_3DView->resetItems();
}

void ContextManager::handleKeyPress(QKeyEvent *e)
{
    int key = e->key();

    /* Do not propagate single modifiers events */
    if (key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift || key == Qt::Key_Meta)
        return;

    qDebug() << "Key press event received:" << e->text();

    if (e->modifiers() & Qt::ControlModifier)
    {
        switch(e->key())
        {
            case Qt::Key_A:
                toggleFixturesSelection();
            break;
            case Qt::Key_Tab:
                selectNextFixtureGroup();
            break;
            case Qt::Key_P:
                setPositionPicking(true);
            break;
            case Qt::Key_R:
                resetDumpValues();
            break;
            case Qt::Key_S:
                QMetaObject::invokeMethod(m_view->rootObject(), "saveProject");
            break;
            case Qt::Key_Z:
                if (e->modifiers() & Qt::ShiftModifier)
                    Tardis::instance()->redoAction();
                else
                    Tardis::instance()->undoAction();
            break;
            default:
            break;
        }
    }

    // 'Delete' key has its own handling
    if (e->key() == Qt::Key_Delete)
    {
        // When a Function editor is open, the selection belongs to the editor
        // (e.g. the Scene Editor fixture list or the EFX Editor head list), so
        // the editor decides what to delete. Never delete Fixtures or Functions
        // from the project while editing.
        // Show items and Tracks are excluded, as those are explicitly clicked
        // on the Show Manager timeline, which can be visible while editing.
        if (m_editingEnabled &&
            m_lastClickedType != App::ShowDragItem &&
            m_lastClickedType != App::TrackDragItem)
        {
            if (m_functionManager->deleteCurrentEditorItems())
                return;
        }

        switch (m_lastClickedType)
        {
            case App::FixtureDragItem:
                m_fixtureManager->deleteFixtures(selectedItemIDVariantList());
                m_fixtureManager->resetCapabilities();
            break;
            case App::FixtureGroupDragItem:
                //m_fixtureManager->deleteFixtureGroups(); // TODO
            break;
            case App::FunctionDragItem:
                m_functionManager->deleteFunctions(m_functionManager->selectedFunctionsID());
            break;
            case App::FolderDragItem:
                m_functionManager->deleteSelectedFolders();
            break;
            case App::ShowDragItem:
            {
                PreviewContext *ctx = contextByName("SHOWMGR");
                if (ctx != nullptr)
                {
                    ShowManager *showMgr = qobject_cast<ShowManager *>(ctx);
                    if (showMgr != nullptr)
                        showMgr->deleteShowItems(showMgr->selectedItemRefs());
                }
            }
            break;
            case App::TrackDragItem:
            {
                PreviewContext *ctx = contextByName("SHOWMGR");
                if (ctx != nullptr)
                {
                    ShowManager *showMgr = qobject_cast<ShowManager *>(ctx);
                    if (showMgr != nullptr)
                        showMgr->deleteSelectedTrack();
                }
            }
            break;
            case App::PaletteDragItem:
            break;
            case App::WidgetDragItem:
            break;
        }

        // Don't let it go through
        return;
    }

    for (PreviewContext *context : m_contextsMap.values()) // C++11
        context->handleKeyEvent(e, true);
}

void ContextManager::handleKeyRelease(QKeyEvent *e)
{
    int key = e->key();

    /* Do not propagate single modifiers events */
    if (key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift || key == Qt::Key_Meta)
        return;

    qDebug() << "Key release event received:" << e->text();

    for (PreviewContext *context : m_contextsMap.values()) // C++11
        context->handleKeyEvent(e, false);
}

/*********************************************************************
 * Universe filtering
 *********************************************************************/

quint32 ContextManager::universeFilter() const
{
    return m_universeFilter;
}

void ContextManager::setUniverseFilter(quint32 universeFilter)
{
    if (m_universeFilter == universeFilter)
        return;

    m_universeFilter = universeFilter;

    m_DMXView->setUniverseFilter(m_universeFilter);
    m_2DView->setUniverseFilter(m_universeFilter);
    m_3DView->setUniverseFilter(m_universeFilter);

    emit universeFilterChanged(universeFilter);
}

/*********************************************************************
 * Common fixture helpers
 *********************************************************************/

void ContextManager::setItemSelection(quint32 itemID, bool enable, int keyModifiers)
{
    qDebug() << "ItemID" << itemID << "enable" << enable << "keymods" << keyModifiers;
    if (enable && keyModifiers == 0 && m_multipleSelection == false)
    {
        resetFixtureSelection();
    }

    quint32 fxID = FixtureUtils::itemFixtureID(itemID);
    Fixture *fixture = m_doc->fixture(fxID);
    if (fixture == nullptr)
        return;

    if (fixture->type() == QLCFixtureDef::Dimmer)
    {
        setFixtureSelection(itemID, FixtureUtils::itemHeadIndex(itemID), enable);
    }
    else
    {
        setFixtureSelection(itemID, -1, enable);
    }
    setLastClickedType(App::FixtureDragItem);
}

void ContextManager::setFixtureSelection(quint32 itemID, int headIndex, bool enable)
{
    quint32 fixtureID = FixtureUtils::itemFixtureID(itemID);
    int linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
    int headIdx = FixtureUtils::itemHeadIndex(itemID);

    if (enable)
    {
        if (m_batchSelection == false)
            qDebug() << "Selected itemID" << itemID << ", fixture ID" << fixtureID << ", head from item" << headIdx << "head passed" << headIndex;
    }

    Fixture *fixture = m_doc->fixture(fixtureID);
    if (fixture == nullptr)
        return;

    QLCFixtureDef::FixtureType type = fixture->type();

    if (m_selectedFixtures.contains(itemID))
    {
        if (enable == false)
            m_selectedFixtures.removeAll(itemID);
        else
            return;
        if (type == QLCFixtureDef::Dimmer && m_selectedDimmersCount > 0)
        {
            m_selectedDimmersCount--;
            if (m_batchSelection == false)
                emit selectedDimmersCountChanged();
        }
    }
    else
    {
        if (enable)
        {
            quint32 flags = m_monProps->fixtureFlags(fixtureID, headIdx, linkedIndex);

            // do not even select a hidden item
            if (flags & MonitorProperties::HiddenFlag)
                return;

            m_selectedFixtures.append(itemID);
            setLastClickedType(App::FixtureDragItem);

            if (type == QLCFixtureDef::Dimmer)
            {
                m_selectedDimmersCount++;
                if (m_batchSelection == false)
                    emit selectedDimmersCountChanged();
            }
        }
        else
            return;
    }

    if (m_batchSelection)
    {
        if (headIndex == -1)
            m_fixtureManager->setItemRoleData(itemID, enable ? 2 : 0, TreeModel::IsSelectedRole);

        QMultiHash<int, SceneValue> channels = m_fixtureManager->getFixtureCapabilities(itemID, headIndex, enable);
        if (channels.keys().isEmpty())
            return;

        QMultiHashIterator<int, SceneValue> it(channels);
        while (it.hasNext())
        {
            it.next();
            quint32 chType = it.key();
            SceneValue sv = it.value();
            if (enable)
                m_channelsMap.insert(chType, sv);
            else
                m_channelsMap.remove(chType, sv);
        }
        return;
    }

    emit dumpValuesCountChanged();

    if (headIndex == -1)
        m_fixtureManager->setItemRoleData(itemID, enable ? 2 : 0, TreeModel::IsSelectedRole);

    if (m_DMXView->isEnabled())
        m_DMXView->updateFixtureSelection(fixtureID, enable);
    if (m_2DView->isEnabled())
        m_2DView->updateFixtureSelection(itemID, enable);
    if (m_3DView->isEnabled())
        m_3DView->updateFixtureSelection(itemID, enable);

    refreshGroupSelectionRoles();

    QMultiHash<int, SceneValue> channels = m_fixtureManager->getFixtureCapabilities(itemID, headIndex, enable);
    if (channels.keys().isEmpty())
        return;

    qDebug() << "[ContextManager] found" << channels.keys().count() << "capabilities";

    QMultiHashIterator<int, SceneValue> it(channels);
    while (it.hasNext())
    {
        it.next();
        quint32 chType = it.key();
        SceneValue sv = it.value();
        if (enable)
            m_channelsMap.insert(chType, sv);
        else
            m_channelsMap.remove(chType, sv);
    }
    emit selectedFixturesChanged();
    emit fixturesPositionChanged();
    emit fixturesRotationChanged();
    emit fixtureDmxTransformFlagsChanged();
    emit fixtureDmxScaleChanged();

    // parachute if we get out of sync
    if (m_selectedFixtures.isEmpty())
        m_fixtureManager->resetCapabilities();
}

void ContextManager::setFixtureIDSelection(quint32 fixtureID, bool enable)
{
    if (fixtureID == Fixture::invalidId())
        return;

    for (quint32 &subID : m_monProps->fixtureIDList(fixtureID))
    {
        quint16 headIndex = m_monProps->fixtureHeadIndex(subID);
        quint16 linkedIndex = m_monProps->fixtureLinkedIndex(subID);

        if (headIndex != 0)
            continue;

        quint32 itemID = FixtureUtils::fixtureItemID(fixtureID, headIndex, linkedIndex);
        setFixtureSelection(itemID, -1, enable);
    }
}

void ContextManager::resetFixtureSelection()
{
    QList<quint32> tmpList = m_selectedFixtures;

    setBatchSelection(true);
    for (quint32 &itemID : tmpList)
        setFixtureSelection(itemID, -1, false);
    setBatchSelection(false);

    m_selectedFixtures.clear();
    m_channelsMap.clear();
}

void ContextManager::toggleFixturesSelection()
{
    bool selectAll = true;
    int visibleCount = 0;

    for (quint32 &fixtureID : m_monProps->fixtureItemsID())
    {
        for (quint32 &subID : m_monProps->fixtureIDList(fixtureID))
        {
            quint16 headIndex = m_monProps->fixtureHeadIndex(subID);
            quint16 linkedIndex = m_monProps->fixtureLinkedIndex(subID);
            int flags = m_monProps->fixtureFlags(fixtureID, headIndex, linkedIndex);
            if (!(flags & MonitorProperties::HiddenFlag))
                visibleCount++;
        }
    }

    if (m_selectedFixtures.count() == visibleCount)
        selectAll = false;

    setBatchSelection(true);
    for (Fixture *fixture : m_doc->fixtures()) // C++11
    {
        if (fixture == nullptr)
            continue;

        for (quint32 &subID : m_monProps->fixtureIDList(fixture->id()))
        {
            quint16 headIndex = m_monProps->fixtureHeadIndex(subID);
            quint16 linkedIndex = m_monProps->fixtureLinkedIndex(subID);
            quint32 itemID = FixtureUtils::fixtureItemID(fixture->id(), headIndex, linkedIndex);
            setFixtureSelection(itemID, -1, selectAll);
        }
    }
    setBatchSelection(false);
}

void ContextManager::setRectangleSelection(qreal x, qreal y, qreal width, qreal height, int keyModifiers)
{
    QList<quint32> fxIDList;

    if (keyModifiers == 0 && multipleSelection() == false)
        resetFixtureSelection();

    if (m_2DView->isEnabled())
        fxIDList = m_2DView->selectFixturesRect(QRectF(x, y, width, height));

    setBatchSelection(true);
    for (quint32 itemID : std::as_const(fxIDList))
        setFixtureSelection(itemID, -1, true);
    setBatchSelection(false);
}

void ContextManager::selectEvenOdd(bool even)
{
    QList<quint32> toRemove;
    for (int i = 0; i < m_selectedFixtures.count(); i++)
    {
        if (even)
        {
            if (i % 2 == 0) // Odd index in 1-based (1, 3, 5...)
                toRemove.append(m_selectedFixtures.at(i));
        }
        else
        {
            if (i % 2 != 0) // Even index in 1-based (2, 4, 6...)
                toRemove.append(m_selectedFixtures.at(i));
        }
    }

    setBatchSelection(true);
    for (quint32 itemID : toRemove)
        setFixtureSelection(itemID, -1, false);
    setBatchSelection(false);
}

void ContextManager::selectEveryNth(int n)
{
    if (n <= 1)
        return;

    QList<quint32> toRemove;
    for (int i = 0; i < m_selectedFixtures.count(); i++)
    {
        if (i % n != 0)
            toRemove.append(m_selectedFixtures.at(i));
    }

    setBatchSelection(true);
    for (quint32 itemID : toRemove)
        setFixtureSelection(itemID, -1, false);
    setBatchSelection(false);
}

QVariantList ContextManager::selectedFixtureAddress()
{
    QVariantList addresses;
    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture == nullptr)
            continue;

        quint32 startAddr = fixture->address();
        for (quint32 i = 0; i < fixture->channels(); i++)
            addresses.append(startAddr + i);
    }

    std::sort(addresses.begin(), addresses.end(),
              [](QVariant a, QVariant b) {
                  return a.toUInt() < b.toUInt();
              });

    return addresses;
}

QVariantList ContextManager::selectedFixtureIDVariantList()
{
    QVariantList list;
    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        list.append(fxID);
    }

    return list;
}

QVariantList ContextManager::selectedItemIDVariantList()
{
    QVariantList list;
    for (quint32 &itemID : m_selectedFixtures)
        list.append(itemID);

    return list;
}

int ContextManager::selectedFixturesCount()
{
    return m_selectedFixtures.count();
}

int ContextManager::selectedDimmersCount()
{
    return m_selectedDimmersCount;
}

bool ContextManager::isFixtureSelected(quint32 itemID)
{
    return m_selectedFixtures.contains(itemID) ? true : false;
}

void ContextManager::setFixturePosition(quint32 itemID, qreal x, qreal y, qreal z)
{
    quint32 fxID = FixtureUtils::itemFixtureID(itemID);
    quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
    quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

    // do not move locked items
    if (m_monProps->fixtureFlags(fxID, headIndex, linkedIndex) & MonitorProperties::LockedFlag)
        return;

    QVector3D currPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
    QVector3D newPos(x, y, z);

    Tardis::instance()->enqueueAction(Tardis::FixtureSetPosition, itemID, QVariant(currPos), QVariant(newPos));
    m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, newPos);

    if (m_2DView->isEnabled())
        m_2DView->updateFixturePosition(itemID, newPos);
    if (m_3DView->isEnabled())
        m_3DView->updateFixturePosition(itemID, newPos);
}

void ContextManager::setFixturesOffset(qreal x, qreal y)
{
    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

        // do not move locked items
        if (m_monProps->fixtureFlags(fxID, headIndex, linkedIndex) & MonitorProperties::LockedFlag)
            continue;

        Fixture *fixture = m_doc->fixture(fxID);

        // Fixtures with their own PositionX/PositionZ DMX channels drag as a
        // live DMX offset, anchored to the stage/grid's own center (matching
        // the "50% = center" convention documented for the user's own
        // real-world fixture profiles) rather than to wherever the fixture
        // happens to be manually placed - the persisted MonitorProperties
        // position is never touched for these. Only meaningful in TopView,
        // since that's the only 2D orientation where the drag's own x/y map
        // directly onto world X/Z (see FixtureUtils::item2DPosition() and
        // this function's own TopView case just below) - world Y is the
        // vertical axis (see MainView3D::updateFixturePosition()'s mm -> m
        // conversion), not shown in a top-down 2D plan, so PositionY is left
        // untouched here exactly like the 3D-only PositionY case is.
        if (fixture != nullptr && m_monProps->pointOfView() == MonitorProperties::TopView &&
            (fixture->channelNumber(QLCChannel::PositionX, QLCChannel::MSB) != QLCChannel::invalid() ||
             fixture->channelNumber(QLCChannel::PositionZ, QLCChannel::MSB) != QLCChannel::invalid()))
        {
            QVector3D gridCenter = FixtureUtils::gridCenterPosition(m_monProps);
            QVector3D newDelta = cachedPositionDelta(fxID, fixture) + QVector3D(x, 0, y) / 1000.0f;
            pushPositionDelta(fixture, newDelta);
            if (m_2DView->isEnabled())
                m_2DView->updateFixturePosition(itemID, gridCenter + (newDelta * 1000.0f));
            continue;
        }

        QVector3D currPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
        QVector3D newPos;

        switch (m_monProps->pointOfView())
        {
            case MonitorProperties::TopView:
                newPos = QVector3D(currPos.x() + x, currPos.y(), currPos.z() + y);
            break;
            case MonitorProperties::RightSideView:
                newPos = QVector3D(currPos.x(), currPos.y() - y, currPos.z() - x);
            break;
            case MonitorProperties::LeftSideView:
                newPos = QVector3D(currPos.x(), currPos.y() - y, currPos.z() + x);
            break;
            default:
                newPos = QVector3D(currPos.x() + x, currPos.y() - y, currPos.z());
            break;
        }

        Tardis::instance()->enqueueAction(Tardis::FixtureSetPosition, itemID, QVariant(currPos), QVariant(newPos));
        m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, newPos);
        if (m_2DView->isEnabled())
            m_2DView->updateFixturePosition(itemID, newPos);
        if (m_3DView->isEnabled())
            m_3DView->updateFixturePosition(itemID, newPos);
    }
}

void ContextManager::pushPositionDelta(Fixture *fixture, QVector3D deltaMeters)
{
    const QLCChannel::Group axisGroups[3] = {
        QLCChannel::PositionX, QLCChannel::PositionY, QLCChannel::PositionZ
    };
    const MonitorProperties::ItemFlags invertFlags[3] = {
        MonitorProperties::InvertedPositionXFlag,
        MonitorProperties::InvertedPositionYFlag,
        MonitorProperties::InvertedPositionZFlag
    };
    const float deltaValues[3] = { deltaMeters.x(), deltaMeters.y(), deltaMeters.z() };

    // $deltaMeters is in view space, i.e. already has this fixture's own
    // invert/scale applied (it comes from cachedPositionDelta(), seeded from
    // FixtureUtils::fixturePositionDelta() - see that function's own note).
    // To write it back onto the raw DMX channel, undo the exact same
    // transform: un-invert, then divide out the scale - the inverse of
    // "rawDelta * scale * sign" is "(viewDelta / scale) * sign" (dividing and
    // multiplying by the same +/-1 sign are identical). Getting this
    // direction wrong (or skipping it) makes a drag jump or drift, since the
    // read path (fixturePositionDelta()) and this write path would then
    // disagree about what a given raw value means.
    quint32 flags = m_monProps->fixtureFlags(fixture->id(), 0, 0);
    float scale = m_monProps->fixtureDmxScale(fixture->id(), 0, 0);
    if (qFuzzyIsNull(scale))
        scale = 1.0f;

    for (int i = 0; i < 3; i++)
    {
        if (fixture->channelNumber(axisGroups[i], QLCChannel::MSB) == QLCChannel::invalid())
            continue;

        float adjusted = deltaValues[i] / scale;
        if (flags & invertFlags[i])
            adjusted = -adjusted;

        int raw = FixtureUtils::positionRawFromDelta(adjusted);
        QList<SceneValue> svList = fixture->axisValueToValues(axisGroups[i], raw);
        for (SceneValue &sv : svList)
        {
            if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
                setDumpValue(sv.fxi, sv.channel, sv.value);
            else
                m_functionManager->setChannelValue(sv.fxi, sv.channel, sv.value);
        }
    }

    // Record what we just told this fixture to be - see cachedPositionDelta()
    // for why this must not be re-derived from the fixture's own channel
    // values on the next call instead.
    m_fixturePositionDeltaCache.insert(fixture->id(), deltaMeters);

    // Also persist the equivalent absolute position into MonitorProperties,
    // in the same millimetre/corner-origin convention every other fixture's
    // fixturePosition() already uses (see gridCenterPosition()'s own doc
    // comment) - this is otherwise unused for a fixture with its own
    // PositionX/Y/Z channels (see the "is DMX-driven" branches in
    // setFixturesOffset()/setFixturesPosition()/fixturesPosition(), which
    // deliberately never call setFixturePosition()), so writing it here does
    // not conflict with anything else that reads it.
    //
    // Root-cause fix for a user report: dragging such a fixture only ever
    // pushed the position as a live DMX value (via setDumpValue() above),
    // which Doc::saveXML() never persists (dumped/live channel values are
    // runtime-only, unlike a Scene's stored values) - so the position was
    // silently lost on every save, and reopening the project showed the
    // fixture back at delta 0 (world center). MonitorProperties::saveXML()
    // itself round-trips correctly (see the new fixtureItemsXML() test in
    // monitorproperties_test) - the bug was that nothing upstream of it ever
    // wrote this fixture's position into it in the first place.
    //
    // NOTE: this fixes the *save* side only. Restoring it on *load* still
    // needs a decision this commit deliberately leaves unmade: either
    // re-push the loaded value onto the fixture's live DMX channels at
    // project load (correct, but physically moves real connected hardware
    // the instant a project is opened - a "flag before implementing" case
    // per this project's standing rule), or make the 2D/3D views prefer this
    // persisted value over the live (still-default) DMX-derived one for a
    // fixture's very first render after load, without touching DMX output
    // (cosmetic-only, no hardware side effect, but touches the same
    // updateFixtureItem() path slotUniverseWritten() drives on every single
    // universe write, which is recently-hardened/cache-sensitive code not
    // touched here). See the task report for the tradeoffs.
    m_monProps->setFixturePosition(fixture->id(), 0, 0,
                                   FixtureUtils::gridCenterPosition(m_monProps) + (deltaMeters * 1000.0f));
    m_doc->setModified();
}

void ContextManager::pushRotationDelta(Fixture *fixture, QVector3D deltaDegrees)
{
    const QLCChannel::Group axisGroups[3] = {
        QLCChannel::RotationX, QLCChannel::RotationY, QLCChannel::RotationZ
    };
    const MonitorProperties::ItemFlags invertFlags[3] = {
        MonitorProperties::InvertedRotationXFlag,
        MonitorProperties::InvertedRotationYFlag,
        MonitorProperties::InvertedRotationZFlag
    };
    const float deltaValues[3] = { deltaDegrees.x(), deltaDegrees.y(), deltaDegrees.z() };

    // See pushPositionDelta()'s equivalent note - same invert/scale inverse.
    quint32 flags = m_monProps->fixtureFlags(fixture->id(), 0, 0);
    float scale = m_monProps->fixtureDmxScale(fixture->id(), 0, 0);
    if (qFuzzyIsNull(scale))
        scale = 1.0f;

    for (int i = 0; i < 3; i++)
    {
        if (fixture->channelNumber(axisGroups[i], QLCChannel::MSB) == QLCChannel::invalid())
            continue;

        float adjusted = deltaValues[i] / scale;
        if (flags & invertFlags[i])
            adjusted = -adjusted;

        int raw = FixtureUtils::rotationRawFromDelta(adjusted);
        QList<SceneValue> svList = fixture->axisValueToValues(axisGroups[i], raw);
        for (SceneValue &sv : svList)
        {
            if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
                setDumpValue(sv.fxi, sv.channel, sv.value);
            else
                m_functionManager->setChannelValue(sv.fxi, sv.channel, sv.value);
        }
    }

    // See pushPositionDelta()'s equivalent note.
    m_fixtureRotationDeltaCache.insert(fixture->id(), deltaDegrees);

    // Persist alongside the position fix above, for the same reason -
    // fixtureRotation() already represents absolute degrees for a
    // non-DMX-driven fixture, so no unit conversion is needed here.
    m_monProps->setFixtureRotation(fixture->id(), 0, 0, deltaDegrees);
    m_doc->setModified();
}

QVector3D ContextManager::cachedPositionDelta(quint32 fxID, Fixture *fixture) const
{
    QHash<quint32, QVector3D>::const_iterator it = m_fixturePositionDeltaCache.constFind(fxID);
    if (it != m_fixturePositionDeltaCache.constEnd())
        return it.value();

    QVector3D delta = FixtureUtils::fixturePositionDelta(fixture, m_monProps);
    m_fixturePositionDeltaCache.insert(fxID, delta);
    return delta;
}

QVector3D ContextManager::cachedRotationDelta(quint32 fxID, Fixture *fixture) const
{
    QHash<quint32, QVector3D>::const_iterator it = m_fixtureRotationDeltaCache.constFind(fxID);
    if (it != m_fixtureRotationDeltaCache.constEnd())
        return it.value();

    QVector3D delta = FixtureUtils::fixtureRotationDelta(fixture, m_monProps);
    m_fixtureRotationDeltaCache.insert(fxID, delta);
    return delta;
}

QVector3D ContextManager::fixturesPosition() const
{
    if (m_selectedFixtures.count() == 1)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(m_selectedFixtures.first());
        quint16 headIndex = FixtureUtils::itemHeadIndex(m_selectedFixtures.first());
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(m_selectedFixtures.first());
        Fixture *fixture = m_doc->fixture(fxID);

        // A fixture with its own PositionX/Y/Z channels is positioned
        // absolutely, anchored to the stage/grid's own center (see
        // setFixturesPosition() below) - read back the DMX-derived position
        // so read-drag-write (as the 3D view's mouse handler does every
        // onPositionChanged event) sees the actual visual position.
        if (fixture != nullptr &&
            (fixture->channelNumber(QLCChannel::PositionX, QLCChannel::MSB) != QLCChannel::invalid() ||
             fixture->channelNumber(QLCChannel::PositionY, QLCChannel::MSB) != QLCChannel::invalid() ||
             fixture->channelNumber(QLCChannel::PositionZ, QLCChannel::MSB) != QLCChannel::invalid()))
        {
            return FixtureUtils::gridCenterPosition(m_monProps) + (cachedPositionDelta(fxID, fixture) * 1000.0f);
        }

        return m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
    }

    return QVector3D(0, 0, 0);
}

void ContextManager::setFixturesPosition(QVector3D position)
{
    if (m_selectedFixtures.isEmpty())
        return;

    if (m_selectedFixtures.count() == 1)
    {
        quint32 itemID = m_selectedFixtures.first();
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

        // do not move locked items
        if (m_monProps->fixtureFlags(fxID, headIndex, linkedIndex) & MonitorProperties::LockedFlag)
            return;

        Fixture *fixture = m_doc->fixture(fxID);

        // Fixtures with their own PositionX/Y/Z DMX channels are positioned
        // absolutely, anchored to the stage/grid's own center - never the
        // persisted MonitorProperties placement - same principle as
        // setFixturesOffset()'s TopView case above.
        if (fixture != nullptr &&
            (fixture->channelNumber(QLCChannel::PositionX, QLCChannel::MSB) != QLCChannel::invalid() ||
             fixture->channelNumber(QLCChannel::PositionY, QLCChannel::MSB) != QLCChannel::invalid() ||
             fixture->channelNumber(QLCChannel::PositionZ, QLCChannel::MSB) != QLCChannel::invalid()))
        {
            QVector3D gridCenter = FixtureUtils::gridCenterPosition(m_monProps);
            pushPositionDelta(fixture, (position - gridCenter) / 1000.0f);
            if (m_3DView->isEnabled())
                m_3DView->updateFixturePosition(itemID, position);
        }
        else
        {
            QVector3D currPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
            Tardis::instance()->enqueueAction(Tardis::FixtureSetPosition, itemID, QVariant(currPos), QVariant(position));

            // absolute position change
            m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, position);
            if (m_3DView->isEnabled())
                m_3DView->updateFixturePosition(m_selectedFixtures.first(), position);
        }
    }
    else
    {
        // relative position change
        for (quint32 &itemID : m_selectedFixtures)
        {
            quint32 fxID = FixtureUtils::itemFixtureID(itemID);
            quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
            quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

            // do not move locked items
            if (m_monProps->fixtureFlags(fxID, headIndex, linkedIndex) & MonitorProperties::LockedFlag)
                continue;

            Fixture *fixture = m_doc->fixture(fxID);

            if (fixture != nullptr &&
                (fixture->channelNumber(QLCChannel::PositionX, QLCChannel::MSB) != QLCChannel::invalid() ||
                 fixture->channelNumber(QLCChannel::PositionY, QLCChannel::MSB) != QLCChannel::invalid() ||
                 fixture->channelNumber(QLCChannel::PositionZ, QLCChannel::MSB) != QLCChannel::invalid()))
            {
                QVector3D newDelta = cachedPositionDelta(fxID, fixture) + (position / 1000.0f);
                pushPositionDelta(fixture, newDelta);
                if (m_3DView->isEnabled())
                    m_3DView->updateFixturePosition(itemID, FixtureUtils::gridCenterPosition(m_monProps) + (newDelta * 1000.0f));
                continue;
            }

            QVector3D currPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
            QVector3D newPos = currPos + position;
            Tardis::instance()->enqueueAction(Tardis::FixtureSetPosition, itemID, QVariant(currPos), QVariant(newPos));

            m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, newPos);
            if (m_3DView->isEnabled())
                m_3DView->updateFixturePosition(itemID, newPos);
        }
    }

    emit fixturesPositionChanged();
}

void ContextManager::setFixturesGelColor(QColor color)
{
    QByteArray ba;
    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        Fixture *fixture = m_doc->fixture(fxID);

        m_monProps->setFixtureGelColor(fxID, headIndex, linkedIndex, color);
        if (m_2DView->isEnabled())
            m_2DView->updateFixtureItem(fixture, headIndex, linkedIndex, ba);
        if (m_3DView->isEnabled())
            m_3DView->updateFixtureItem(fixture, headIndex, linkedIndex, ba);
    }
    m_doc->setModified();
}

void ContextManager::setFixedZoom(int degrees)
{
    QByteArray ba;
    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        Fixture *fixture = m_doc->fixture(fxID);

        if (fixture->type() != QLCFixtureDef::Dimmer)
            continue;

        m_monProps->setFixtureFixedZoom(fxID, headIndex, linkedIndex, degrees);
        if (m_3DView->isEnabled())
            m_3DView->updateFixtureItem(fixture, headIndex, linkedIndex, ba);
    }
}

void ContextManager::setFixturesAlignment(int alignment)
{
    if (m_selectedFixtures.count() == 0)
        return;

    quint32 firstFxID = FixtureUtils::itemFixtureID(m_selectedFixtures.first());
    quint16 firstHeadIndex = FixtureUtils::itemHeadIndex(m_selectedFixtures.first());
    quint16 firstLinkedIndex = FixtureUtils::itemLinkedIndex(m_selectedFixtures.first());
    QVector3D firstPos = m_monProps->fixturePosition(firstFxID, firstHeadIndex, firstLinkedIndex);

    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        QVector3D fxPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);

        FixtureUtils::alignItem(firstPos, fxPos, m_monProps->pointOfView(), alignment);
        m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, fxPos);
        if (m_2DView->isEnabled())
            m_2DView->updateFixturePosition(itemID, fxPos);
        if (m_3DView->isEnabled())
            m_3DView->updateFixturePosition(itemID, fxPos);
    }
    m_doc->setModified();
}

void ContextManager::setFixturesDistribution(int direction)
{
    if (m_selectedFixtures.count() < 3)
        return;

    qreal min = 1000000;
    qreal max = 0;
    qreal fixturesSize = 0;
    qreal gap = 0;
    QVector<quint32> sortedIDs;
    QVector<quint32> sortedPos;

    /* cycle through selected fixtures and do the following:
     * 1- calculate the total width/height
     * 2- sort the fixture IDs from the leftmost/topmost item
     * 3- detect the minimum and maximum items position
     */
    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        Fixture *fixture = m_doc->fixture(fxID);
        QPointF fxPos = FixtureUtils::item2DPosition(m_monProps, m_monProps->pointOfView(),
                                                     m_monProps->fixturePosition(fxID, headIndex, linkedIndex));
        QSizeF fxRect = FixtureUtils::item2DDimension(fixture->fixtureMode(), m_monProps->pointOfView());
        qreal pos = direction == Qt::Horizontal ? fxPos.x() : fxPos.y();
        qreal size = direction == Qt::Horizontal ? fxRect.width() : fxRect.height();
        int i = 0;

        // 1
        fixturesSize += size;

        // 2
        for (i = 0; i < sortedPos.count(); i++)
        {
            if (pos < sortedPos[i])
                break;
        }
        if (sortedPos.isEmpty() || i == sortedIDs.count())
        {
            sortedIDs.append(itemID);
            sortedPos.append(pos);
        }
        else
        {
            sortedIDs.insert(i, itemID);
            sortedPos.insert(i, pos);
        }

        // 3
        if (pos + size > max)
            max = pos + size;
        if (pos < min)
            min = pos;
    }

    gap = ((max - min) - fixturesSize) / (sortedIDs.count() - 1);

    qDebug() << "Sorted IDs:" << sortedIDs << "min/max:" << min << max;

    qreal newPos = min;

    for (int idx = 0; idx < sortedIDs.count(); idx++)
    {
        quint32 itemID = sortedIDs[idx];
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        Fixture *fixture = m_doc->fixture(fxID);
        QSizeF fxRect = FixtureUtils::item2DDimension(fixture->fixtureMode(), m_monProps->pointOfView());
        qreal size = direction == Qt::Horizontal ? fxRect.width() : fxRect.height();
        QVector3D fxPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);

        // the first and last fixture don't need any adjustment
        if (idx > 0 && idx < sortedIDs.count() - 1)
        {
            switch(m_monProps->pointOfView())
            {
                case MonitorProperties::TopView:
                    if (direction == Qt::Horizontal)
                        fxPos.setX(newPos);
                    else
                        fxPos.setZ(newPos);
                break;
                case MonitorProperties::RightSideView:
                    if (direction == Qt::Horizontal)
                        fxPos.setZ(m_monProps->gridSize().z() - newPos);
                    else
                        fxPos.setY(newPos);
                break;
                case MonitorProperties::LeftSideView:
                    if (direction == Qt::Horizontal)
                        fxPos.setZ(newPos);
                    else
                        fxPos.setY(newPos);
                break;
                default:
                    if (direction == Qt::Horizontal)
                        fxPos.setX(newPos);
                    else
                        fxPos.setY(newPos);
                break;
            }

            m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, fxPos);
            if (m_2DView->isEnabled())
                m_2DView->updateFixturePosition(itemID, fxPos);
            if (m_3DView->isEnabled())
                m_3DView->updateFixturePosition(itemID, fxPos);
        }

        newPos += size + gap;
    }
    m_doc->setModified();
}

void ContextManager::fixturePlaneAxes(int pointOfView, int &hAxis, int &vAxis, int &dAxis) const
{
    switch (pointOfView)
    {
        case MonitorProperties::TopView:
            hAxis = 0; vAxis = 2; dAxis = 1; // X / Z, depth is height (Y)
        break;
        case MonitorProperties::RightSideView:
        case MonitorProperties::LeftSideView:
            hAxis = 2; vAxis = 1; dAxis = 0; // Z / Y, depth is left-right (X)
        break;
        case MonitorProperties::Undefined:
        case MonitorProperties::FrontView:
        default:
            hAxis = 0; vAxis = 1; dAxis = 2; // X / Y, depth is front-back (Z)
        break;
    }
}

static qreal vecAxis(const QVector3D &v, int axis)
{
    return axis == 0 ? v.x() : axis == 1 ? v.y() : v.z();
}

static void setVecAxis(QVector3D &v, int axis, qreal value)
{
    if (axis == 0)
        v.setX(value);
    else if (axis == 1)
        v.setY(value);
    else
        v.setZ(value);
}

QVector3D ContextManager::selectedFixturesCentroid() const
{
    QVector3D sum(0, 0, 0);

    for (quint32 itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        sum += m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
    }

    if (m_selectedFixtures.isEmpty())
        return sum;

    return sum / m_selectedFixtures.count();
}

QList<quint32> ContextManager::sortedSelectedFixtures() const
{
    QList<quint32> sorted = m_selectedFixtures;

    std::sort(sorted.begin(), sorted.end(), [this] (quint32 left, quint32 right)
    {
        Fixture *leftFixture = m_doc->fixture(FixtureUtils::itemFixtureID(left));
        Fixture *rightFixture = m_doc->fixture(FixtureUtils::itemFixtureID(right));

        if (leftFixture == nullptr || rightFixture == nullptr)
            return false;

        if (leftFixture != rightFixture)
            return *leftFixture < *rightFixture;

        quint16 leftHead = FixtureUtils::itemHeadIndex(left);
        quint16 rightHead = FixtureUtils::itemHeadIndex(right);
        if (leftHead != rightHead)
            return leftHead < rightHead;

        return FixtureUtils::itemLinkedIndex(left) < FixtureUtils::itemLinkedIndex(right);
    });

    return sorted;
}

QList<quint32> ContextManager::groupOrSortedSelectedFixtures() const
{
    if (m_selectedFixtures.isEmpty())
        return sortedSelectedFixtures();

    // Find a single FixtureGroup that contains every selected fixture
    FixtureGroup *commonGroup = nullptr;
    for (quint32 itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        FixtureGroup *fxGroup = nullptr;

        for (FixtureGroup *group : m_doc->fixtureGroups())
        {
            if (group->fixtureList().contains(fxID))
            {
                fxGroup = group;
                break;
            }
        }

        if (fxGroup == nullptr)
            return sortedSelectedFixtures();

        if (commonGroup == nullptr)
            commonGroup = fxGroup;
        else if (commonGroup != fxGroup)
            return sortedSelectedFixtures();
    }

    // Order the selection by the group's own grid. QLCPoint::operator< sorts
    // row-major (y then x), and headsMap() is a QMap keyed by QLCPoint, so
    // iterating it already yields top-left to bottom-right order.
    QSet<quint32> selectedSet(m_selectedFixtures.begin(), m_selectedFixtures.end());
    QList<quint32> ordered;

    QMapIterator<QLCPoint, GroupHead> it(commonGroup->headsMap());
    while (it.hasNext())
    {
        it.next();
        GroupHead gh = it.value();
        if (gh.isValid() == false)
            continue;

        quint32 itemID = FixtureUtils::fixtureItemID(gh.fxi, gh.head >= 0 ? gh.head : 0, 0);
        if (selectedSet.contains(itemID))
            ordered.append(itemID);
    }

    // Safety net: if the group's heads don't reconstruct the full selection
    // (e.g. a head/linked-index mismatch), fall back rather than silently
    // dropping fixtures from the arrangement.
    if (ordered.count() != m_selectedFixtures.count())
        return sortedSelectedFixtures();

    return ordered;
}

qreal ContextManager::detectedCircleDiameter() const
{
    if (m_selectedFixtures.count() < 2)
        return 0;

    int hAxis, vAxis, dAxis;
    fixturePlaneAxes(m_monProps->pointOfView(), hAxis, vAxis, dAxis);
    Q_UNUSED(dAxis)
    QVector3D centroid = selectedFixturesCentroid();

    qreal sumRadius = 0;
    for (quint32 itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        QVector3D pos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);

        qreal h = vecAxis(pos, hAxis) - vecAxis(centroid, hAxis);
        qreal v = vecAxis(pos, vAxis) - vecAxis(centroid, vAxis);
        sumRadius += qSqrt(h * h + v * v);
    }

    return (sumRadius / m_selectedFixtures.count()) * 2.0;
}

void ContextManager::detectedLineFit(qreal &angleRadians, qreal &length) const
{
    angleRadians = 0;
    length = 0;

    if (m_selectedFixtures.count() < 2)
        return;

    int hAxis, vAxis, dAxis;
    fixturePlaneAxes(m_monProps->pointOfView(), hAxis, vAxis, dAxis);
    Q_UNUSED(dAxis)
    QVector3D centroid = selectedFixturesCentroid();

    // Principal axis of the position scatter, via the dominant eigenvector of
    // its 2D covariance matrix - the same "structure tensor" formula used to
    // recover a blob's orientation in image processing.
    QVector<QPointF> local;
    local.reserve(m_selectedFixtures.count());
    qreal sxx = 0, syy = 0, sxy = 0;

    for (quint32 itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        QVector3D pos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);

        qreal h = vecAxis(pos, hAxis) - vecAxis(centroid, hAxis);
        qreal v = vecAxis(pos, vAxis) - vecAxis(centroid, vAxis);
        local.append(QPointF(h, v));

        sxx += h * h;
        syy += v * v;
        sxy += h * v;
    }

    if (qFuzzyIsNull(sxx) && qFuzzyIsNull(syy) && qFuzzyIsNull(sxy))
        return; // every fixture sits on top of its neighbours - no direction to detect

    angleRadians = 0.5 * qAtan2(2.0 * sxy, sxx - syy);
    qreal dirH = qCos(angleRadians);
    qreal dirV = qSin(angleRadians);

    qreal minProj = 0, maxProj = 0;
    for (int i = 0; i < local.count(); i++)
    {
        qreal proj = local.at(i).x() * dirH + local.at(i).y() * dirV;
        if (i == 0 || proj < minProj)
            minProj = proj;
        if (i == 0 || proj > maxProj)
            maxProj = proj;
    }

    length = maxProj - minProj;
}

qreal ContextManager::detectedLineLength() const
{
    qreal angleRadians, length;
    detectedLineFit(angleRadians, length);
    return length;
}

qreal ContextManager::detectedLineAngle() const
{
    qreal angleRadians, length;
    detectedLineFit(angleRadians, length);
    return qRadiansToDegrees(angleRadians);
}

void ContextManager::faceFixtureTowards(quint32 itemID, const QVector3D &newPos, const QVector3D &centroid,
                                         int hAxis, int vAxis, int dAxis)
{
    quint32 fxID = FixtureUtils::itemFixtureID(itemID);
    quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
    quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

    qreal dh = vecAxis(centroid, hAxis) - vecAxis(newPos, hAxis);
    qreal dv = vecAxis(centroid, vAxis) - vecAxis(newPos, vAxis);
    if (qFuzzyIsNull(dh) && qFuzzyIsNull(dv))
        return; // fixture is sitting right on the centroid - no facing to compute

    qreal bearingDeg = qRadiansToDegrees(qAtan2(dv, dh));

    QVector3D currRot = m_monProps->fixtureRotation(fxID, headIndex, linkedIndex);
    QVector3D newRot = currRot;
    setVecAxis(newRot, dAxis, bearingDeg);

    Tardis::instance()->enqueueAction(Tardis::FixtureSetRotation, itemID, QVariant(currRot), QVariant(newRot));
    m_monProps->setFixtureRotation(fxID, headIndex, linkedIndex, newRot);

    if (m_2DView->isEnabled())
        m_2DView->updateFixtureRotation(itemID, newRot);
    if (m_3DView->isEnabled())
        m_3DView->updateFixtureRotation(itemID, newRot);
}

void ContextManager::arrangeFixturesInCircle(qreal diameter, bool lookAtCenter)
{
    QList<quint32> fixtures = sortedSelectedFixtures();
    int count = fixtures.count();
    if (count == 0)
        return;

    int hAxis, vAxis, dAxis;
    fixturePlaneAxes(m_monProps->pointOfView(), hAxis, vAxis, dAxis);
    QVector3D centroid = selectedFixturesCentroid();
    qreal radius = diameter / 2.0;

    for (int i = 0; i < count; i++)
    {
        quint32 itemID = fixtures.at(i);
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

        if (m_monProps->fixtureFlags(fxID, headIndex, linkedIndex) & MonitorProperties::LockedFlag)
            continue;

        qreal angleRad = qDegreesToRadians(360.0 * i / count);
        QVector3D newPos = centroid;
        setVecAxis(newPos, hAxis, vecAxis(centroid, hAxis) + radius * qCos(angleRad));
        setVecAxis(newPos, vAxis, vecAxis(centroid, vAxis) + radius * qSin(angleRad));

        QVector3D currPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
        Tardis::instance()->enqueueAction(Tardis::FixtureSetPosition, itemID, QVariant(currPos), QVariant(newPos));
        m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, newPos);

        if (m_2DView->isEnabled())
            m_2DView->updateFixturePosition(itemID, newPos);
        if (m_3DView->isEnabled())
            m_3DView->updateFixturePosition(itemID, newPos);

        if (lookAtCenter)
            faceFixtureTowards(itemID, newPos, centroid, hAxis, vAxis, dAxis);
    }

    m_doc->setModified();
    emit fixturesPositionChanged();
    if (lookAtCenter)
        emit fixturesRotationChanged();
}

void ContextManager::arrangeFixturesInGrid(qreal width, qreal height, int columns, qreal angleDegrees)
{
    // Prefer the selection's own Fixture Group grid order (what RGB Matrix
    // effects actually run on) over plain DMX order, when applicable.
    QList<quint32> fixtures = groupOrSortedSelectedFixtures();
    int count = fixtures.count();
    if (count == 0)
        return;

    if (columns <= 0)
        columns = qCeil(qSqrt(qreal(count)));
    int rows = qCeil(qreal(count) / columns);

    int hAxis, vAxis, dAxis;
    fixturePlaneAxes(m_monProps->pointOfView(), hAxis, vAxis, dAxis);
    Q_UNUSED(dAxis)
    QVector3D centroid = selectedFixturesCentroid();

    qreal colStep = columns > 1 ? width / (columns - 1) : 0;
    qreal rowStep = rows > 1 ? height / (rows - 1) : 0;
    qreal angleRad = qDegreesToRadians(angleDegrees);

    for (int i = 0; i < count; i++)
    {
        quint32 itemID = fixtures.at(i);
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

        if (m_monProps->fixtureFlags(fxID, headIndex, linkedIndex) & MonitorProperties::LockedFlag)
            continue;

        int col = i % columns;
        int row = i / columns;
        qreal h = -width / 2.0 + col * colStep;
        qreal v = -height / 2.0 + row * rowStep;

        qreal rh = h * qCos(angleRad) - v * qSin(angleRad);
        qreal rv = h * qSin(angleRad) + v * qCos(angleRad);

        QVector3D newPos = centroid;
        setVecAxis(newPos, hAxis, vecAxis(centroid, hAxis) + rh);
        setVecAxis(newPos, vAxis, vecAxis(centroid, vAxis) + rv);

        QVector3D currPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
        Tardis::instance()->enqueueAction(Tardis::FixtureSetPosition, itemID, QVariant(currPos), QVariant(newPos));
        m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, newPos);

        if (m_2DView->isEnabled())
            m_2DView->updateFixturePosition(itemID, newPos);
        if (m_3DView->isEnabled())
            m_3DView->updateFixturePosition(itemID, newPos);
    }

    m_doc->setModified();
    emit fixturesPositionChanged();
}

void ContextManager::arrangeFixturesInLine(qreal length, qreal angleDegrees, bool lookAtCenter)
{
    QList<quint32> fixtures = sortedSelectedFixtures();
    int count = fixtures.count();
    if (count == 0)
        return;

    int hAxis, vAxis, dAxis;
    fixturePlaneAxes(m_monProps->pointOfView(), hAxis, vAxis, dAxis);
    QVector3D centroid = selectedFixturesCentroid();

    qreal angleRad = qDegreesToRadians(angleDegrees);
    qreal step = count > 1 ? length / (count - 1) : 0;
    qreal startOffset = -length / 2.0;

    for (int i = 0; i < count; i++)
    {
        quint32 itemID = fixtures.at(i);
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

        if (m_monProps->fixtureFlags(fxID, headIndex, linkedIndex) & MonitorProperties::LockedFlag)
            continue;

        qreal dist = startOffset + i * step;
        QVector3D newPos = centroid;
        setVecAxis(newPos, hAxis, vecAxis(centroid, hAxis) + dist * qCos(angleRad));
        setVecAxis(newPos, vAxis, vecAxis(centroid, vAxis) + dist * qSin(angleRad));

        QVector3D currPos = m_monProps->fixturePosition(fxID, headIndex, linkedIndex);
        Tardis::instance()->enqueueAction(Tardis::FixtureSetPosition, itemID, QVariant(currPos), QVariant(newPos));
        m_monProps->setFixturePosition(fxID, headIndex, linkedIndex, newPos);

        if (m_2DView->isEnabled())
            m_2DView->updateFixturePosition(itemID, newPos);
        if (m_3DView->isEnabled())
            m_3DView->updateFixturePosition(itemID, newPos);

        if (lookAtCenter)
            faceFixtureTowards(itemID, newPos, centroid, hAxis, vAxis, dAxis);
    }

    m_doc->setModified();
    emit fixturesPositionChanged();
    if (lookAtCenter)
        emit fixturesRotationChanged();
}

void ContextManager::setLinkedFixture(quint32 itemID)
{
    quint32 fixtureID = FixtureUtils::itemFixtureID(itemID);
    int headIndex = FixtureUtils::itemHeadIndex(itemID);
    int linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

    if (linkedIndex)
    {
        // remove an existing linked fixture - itemID is the linked fixture

        // 1- remove the node from Fixture Manager
        m_fixtureManager->updateLinkedFixtureNode(itemID, false);

        // 2- remove the item from previews
        if (m_2DView->isEnabled())
            m_2DView->removeFixtureItem(itemID);
        if (m_3DView->isEnabled())
            m_3DView->removeFixtureItem(itemID);

        // 3- remove the item from Monitor properties
        m_monProps->removeFixture(fixtureID, headIndex, linkedIndex);
    }
    else
    {
        // create a new linked fixture - itemID is the base fixture
        int newIndex = 1;
        Fixture *fixture = m_doc->fixture(fixtureID);
        if (fixture == nullptr)
            return;

        // 1- iterate through Fixture subitems to find a new linked index
        for (quint32 &subID : m_monProps->fixtureIDList(fixtureID))
        {
            quint16 hIdx = m_monProps->fixtureHeadIndex(subID);
            quint16 lIdx = m_monProps->fixtureLinkedIndex(subID);
            if (hIdx != headIndex)
                continue;

            if (lIdx >= newIndex)
                newIndex = lIdx + 1;
        }
        // 2- find a position for the new item
        QVector3D pos = m_monProps->fixturePosition(fixtureID, headIndex, linkedIndex);
        QLCPhysical phy = fixture->fixtureMode()->physical();
        if (m_monProps->pointOfView() == MonitorProperties::TopView)
            pos.setZ(pos.z() + phy.depth() + 50);
        else
            pos.setY(pos.y() + phy.height() + 50);

        // 3- add the new item to monitor properties
        QString newName = QString("%1 (%2 %3)").arg(fixture->name(), tr("linked")).arg(newIndex);
        m_monProps->setFixturePosition(fixtureID, headIndex, newIndex, pos);
        m_monProps->setFixtureName(fixtureID, headIndex, newIndex, newName);

        // 4- add the new item to the Fixture Manager tree
        quint32 linkedItemID = FixtureUtils::fixtureItemID(fixtureID, headIndex, newIndex);
        m_fixtureManager->updateLinkedFixtureNode(linkedItemID, true);

        // 5- create the new item in the previews
        if (m_2DView->isEnabled())
            m_2DView->createFixtureItem(fixtureID, headIndex, newIndex, pos, false);
        if (m_3DView->isEnabled())
            m_3DView->createFixtureItem(fixtureID, headIndex, newIndex, pos, false);
    }
}

void ContextManager::updateFixturesCapabilities()
{
    for (quint32 &itemID : m_selectedFixtures)
        m_fixtureManager->getFixtureCapabilities(itemID, -1, true);
}

qreal ContextManager::getCurrentValue(int type, bool degrees)
{
    qreal currMsbValue = -1;
    qreal currLsbValue = -1;
    qreal currValue = -1;

    QList<SceneValue> svList = m_channelsMap.values(type);
    for (SceneValue &sv : svList)
    {
        Fixture *fixture = m_doc->fixture(sv.fxi);
        if (fixture == nullptr)
            continue;

        const QLCChannel *ch = fixture->channel(sv.channel);
        if (ch == nullptr)
            continue;

        qreal chValue = fixture->channelValueAt(sv.channel);
        qreal divider = ch->controlByte() == QLCChannel::MSB ? 256.0 : 65536.0;

        if (degrees)
        {
            QLCFixtureMode *fxMode = fixture->fixtureMode();
            QLCPhysical phy = fxMode->physical();
            switch (type)
            {
                case QLCChannel::Pan:
                    chValue = (qreal(phy.focusPanMax()) / divider) * chValue;
                break;
                case QLCChannel::Tilt:
                    chValue = (qreal(phy.focusTiltMax()) / divider) * chValue;
                break;
                case QLCChannel::Beam:
                        chValue = qreal((phy.lensDegreesMax() - phy.lensDegreesMin()) / divider) * chValue;

                        if (ch->preset() == QLCChannel::BeamZoomBigSmall)
                            chValue = phy.lensDegreesMax() - chValue;
                        else if (ch->controlByte() == QLCChannel::MSB)
                            chValue += phy.lensDegreesMin();

                        qDebug() << "Current degrees:" << chValue;
                break;
            }
        }

        if (ch->controlByte() == QLCChannel::MSB)
        {
            if (currMsbValue != -1 && currMsbValue != chValue)
                return -1;

            currMsbValue = chValue;
        }
        else if (ch->controlByte() == QLCChannel::LSB)
        {
            if (currLsbValue != -1 && currLsbValue != chValue)
                return -1;

            currLsbValue = chValue;
        }
    }

    qDebug() << "Channel type" << type << "MSB" << currMsbValue << "LSB" << currLsbValue;
    currValue = currMsbValue + (currLsbValue == -1 ? 0 : currLsbValue);

    return currValue;
}

void ContextManager::getCurrentColors(QQuickItem *item) const
{
    int rgbDiffCount = 0;
    int wauvDiffCount = 0;
    QColor rgbColor;
    QColor wauvColor;

    for (const quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);

        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture == nullptr)
            continue;

        QColor itemRgbColor;
        QColor itemWauvColor;

        QVector <quint32> rgbCh = fixture->rgbChannels(headIndex);
        if (rgbCh.size() == 3)
        {
            itemRgbColor.setRgb(fixture->channelValueAt(rgbCh.at(0)),
                             fixture->channelValueAt(rgbCh.at(1)),
                             fixture->channelValueAt(rgbCh.at(2)));
        }

        QVector <quint32> cmyCh = fixture->cmyChannels(headIndex);
        if (cmyCh.size() == 3)
        {
            itemRgbColor.setCmyk(fixture->channelValueAt(cmyCh.at(0)),
                              fixture->channelValueAt(cmyCh.at(1)),
                              fixture->channelValueAt(cmyCh.at(2)), 0);
        }

        if (rgbDiffCount == 0 || itemRgbColor == rgbColor)
            rgbColor = itemRgbColor;
        else
            rgbDiffCount++;

        quint32 white = fixture->channelNumber(QLCChannel::White, QLCChannel::MSB, headIndex);
        quint32 amber = fixture->channelNumber(QLCChannel::Amber, QLCChannel::MSB, headIndex);
        quint32 UV = fixture->channelNumber(QLCChannel::UV, QLCChannel::MSB, headIndex);

        if (white != QLCChannel::invalid())
            itemWauvColor.setRed(fixture->channelValueAt(white));
        if (amber != QLCChannel::invalid())
            itemWauvColor.setGreen(fixture->channelValueAt(amber));
        if (UV != QLCChannel::invalid())
            itemWauvColor.setBlue(fixture->channelValueAt(UV));

        if (wauvDiffCount == 0 || itemWauvColor == wauvColor)
            wauvColor = itemWauvColor;
        else
            wauvDiffCount++;
    }

    QMetaObject::invokeMethod(item, "updateColors",
                              Q_ARG(QVariant, rgbDiffCount ? false : true),
                              Q_ARG(QVariant, rgbColor),
                              Q_ARG(QVariant, wauvDiffCount ? false : true),
                              Q_ARG(QVariant, wauvColor));
}

void ContextManager::createFixtureGroup()
{
    if (m_selectedFixtures.isEmpty())
        return;

    m_fixtureManager->addItemsToNewGroup(m_selectedFixtures);
}

QVector3D ContextManager::fixturesRotation() const
{
    if (m_selectedFixtures.count() == 1)
    {
        quint32 fixtureID = FixtureUtils::itemFixtureID(m_selectedFixtures.first());
        if (m_monProps->containsFixture(fixtureID) == true)
        {
            quint16 headIndex = FixtureUtils::itemHeadIndex(m_selectedFixtures.first());
            quint16 linkedIndex = FixtureUtils::itemLinkedIndex(m_selectedFixtures.first());

            // A fixture with its own RotationX/Y/Z channels rotates absolutely
            // (identity/0 = no rotation), never relative to a static rest
            // orientation - same reasoning as fixturesPosition() above.
            Fixture *fixture = m_doc->fixture(fixtureID);
            if (fixture != nullptr &&
                (fixture->channelNumber(QLCChannel::RotationX, QLCChannel::MSB) != QLCChannel::invalid() ||
                 fixture->channelNumber(QLCChannel::RotationY, QLCChannel::MSB) != QLCChannel::invalid() ||
                 fixture->channelNumber(QLCChannel::RotationZ, QLCChannel::MSB) != QLCChannel::invalid()))
            {
                return cachedRotationDelta(fixtureID, fixture);
            }

            return m_monProps->fixtureRotation(fixtureID, headIndex, linkedIndex);
        }
    }

    return QVector3D(0, 0, 0);
}

void ContextManager::setFixturesRotation(QVector3D degrees)
{
    if (m_selectedFixtures.count() == 1)
    {
        quint32 itemID = m_selectedFixtures.first();
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
        Fixture *fixture = m_doc->fixture(fxID);

        // Fixtures with their own RotationX/Y/Z DMX channels rotate
        // absolutely (identity/0 = no rotation), never relative to a static
        // rest orientation - same principle as setFixturesPosition().
        if (fixture != nullptr &&
            (fixture->channelNumber(QLCChannel::RotationX, QLCChannel::MSB) != QLCChannel::invalid() ||
             fixture->channelNumber(QLCChannel::RotationY, QLCChannel::MSB) != QLCChannel::invalid() ||
             fixture->channelNumber(QLCChannel::RotationZ, QLCChannel::MSB) != QLCChannel::invalid()))
        {
            pushRotationDelta(fixture, degrees);
            if (m_2DView->isEnabled())
                m_2DView->updateFixtureRotation(itemID, degrees);
            if (m_3DView->isEnabled())
                m_3DView->updateFixtureRotation(itemID, degrees);
        }
        else
        {
            QVector3D rotation = m_monProps->fixtureRotation(fxID, headIndex, linkedIndex);
            Tardis::instance()->enqueueAction(Tardis::FixtureSetRotation, itemID, QVariant(rotation), QVariant(degrees));

            // absolute rotation change
            m_monProps->setFixtureRotation(fxID, headIndex, linkedIndex, degrees);
            if (m_2DView->isEnabled())
                m_2DView->updateFixtureRotation(itemID, degrees);
            if (m_3DView->isEnabled())
                m_3DView->updateFixtureRotation(itemID, degrees);
        }
    }
    else
    {
        // relative rotation change
        for (quint32 &itemID : m_selectedFixtures)
        {
            quint32 fxID = FixtureUtils::itemFixtureID(itemID);
            quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
            quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
            Fixture *fixture = m_doc->fixture(fxID);

            if (fixture != nullptr &&
                (fixture->channelNumber(QLCChannel::RotationX, QLCChannel::MSB) != QLCChannel::invalid() ||
                 fixture->channelNumber(QLCChannel::RotationY, QLCChannel::MSB) != QLCChannel::invalid() ||
                 fixture->channelNumber(QLCChannel::RotationZ, QLCChannel::MSB) != QLCChannel::invalid()))
            {
                QVector3D newDelta = cachedRotationDelta(fxID, fixture) + degrees;
                pushRotationDelta(fixture, newDelta);
                if (m_2DView->isEnabled())
                    m_2DView->updateFixtureRotation(itemID, newDelta);
                if (m_3DView->isEnabled())
                    m_3DView->updateFixtureRotation(itemID, newDelta);
                continue;
            }

            QVector3D rotation = m_monProps->fixtureRotation(fxID, headIndex, linkedIndex);
            QVector3D newRot = rotation + degrees;

            // normalize back to a 0-359 range
            if (newRot.x() < 0) newRot.setX(newRot.x() + 360);
            else if (newRot.x() >= 360) newRot.setX(newRot.x() - 360);

            if (newRot.y() < 0) newRot.setY(newRot.y() + 360);
            else if (newRot.y() >= 360) newRot.setY(newRot.y() - 360);

            if (newRot.z() < 0) newRot.setZ(newRot.z() + 360);
            else if (newRot.z() >= 360) newRot.setZ(newRot.z() - 360);

            Tardis::instance()->enqueueAction(Tardis::FixtureSetRotation, itemID, QVariant(rotation), QVariant(newRot));

            m_monProps->setFixtureRotation(fxID, headIndex, linkedIndex, newRot);
            if (m_2DView->isEnabled())
                m_2DView->updateFixtureRotation(itemID, newRot);
            if (m_3DView->isEnabled())
                m_3DView->updateFixtureRotation(itemID, newRot);
        }
    }

    emit fixturesRotationChanged();
}

void ContextManager::setFixtureRotation(quint32 itemID, QVector3D degrees)
{
    quint32 fxID = FixtureUtils::itemFixtureID(itemID);
    quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
    quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);
    QVector3D rotation = m_monProps->fixtureRotation(fxID, headIndex, linkedIndex);

    Tardis::instance()->enqueueAction(Tardis::FixtureSetRotation, itemID, QVariant(rotation), QVariant(degrees));

    // absolute rotation change
    m_monProps->setFixtureRotation(fxID, headIndex, linkedIndex, degrees);
    if (m_2DView->isEnabled())
        m_2DView->updateFixtureRotation(itemID, degrees);
    if (m_3DView->isEnabled())
        m_3DView->updateFixtureRotation(itemID, degrees);
}

// The 6 axis-invert bits this feature owns within MonitorProperties'
// combined flags bitmask - used to mask reads/writes so
// setFixtureDmxTransformFlags() never clobbers Hidden/Locked/InvertedPan/
// InvertedTiltFlag, which share the same underlying storage.
static const quint32 kDmxTransformFlagsMask =
        MonitorProperties::InvertedPositionXFlag | MonitorProperties::InvertedPositionYFlag |
        MonitorProperties::InvertedPositionZFlag | MonitorProperties::InvertedRotationXFlag |
        MonitorProperties::InvertedRotationYFlag | MonitorProperties::InvertedRotationZFlag;

bool ContextManager::selectedFixtureHasDmxTransform() const
{
    if (m_selectedFixtures.count() != 1)
        return false;

    quint32 fxID = FixtureUtils::itemFixtureID(m_selectedFixtures.first());
    Fixture *fixture = m_doc->fixture(fxID);
    if (fixture == nullptr)
        return false;

    return fixture->channelNumber(QLCChannel::PositionX, QLCChannel::MSB) != QLCChannel::invalid() ||
           fixture->channelNumber(QLCChannel::PositionY, QLCChannel::MSB) != QLCChannel::invalid() ||
           fixture->channelNumber(QLCChannel::PositionZ, QLCChannel::MSB) != QLCChannel::invalid() ||
           fixture->channelNumber(QLCChannel::RotationX, QLCChannel::MSB) != QLCChannel::invalid() ||
           fixture->channelNumber(QLCChannel::RotationY, QLCChannel::MSB) != QLCChannel::invalid() ||
           fixture->channelNumber(QLCChannel::RotationZ, QLCChannel::MSB) != QLCChannel::invalid();
}

quint32 ContextManager::fixtureDmxTransformFlags() const
{
    if (m_selectedFixtures.count() != 1)
        return 0;

    quint32 fxID = FixtureUtils::itemFixtureID(m_selectedFixtures.first());
    return m_monProps->fixtureFlags(fxID, 0, 0) & kDmxTransformFlagsMask;
}

void ContextManager::setFixtureDmxTransformFlags(quint32 flags)
{
    if (m_selectedFixtures.count() != 1)
        return;

    quint32 fxID = FixtureUtils::itemFixtureID(m_selectedFixtures.first());
    quint32 existing = m_monProps->fixtureFlags(fxID, 0, 0);
    quint32 merged = (existing & ~kDmxTransformFlagsMask) | (flags & kDmxTransformFlagsMask);
    if (merged == existing)
        return;

    m_monProps->setFixtureFlags(fxID, 0, 0, merged);
    m_doc->setModified();
    refreshFixtureDmxTransform(fxID);
    emit fixtureDmxTransformFlagsChanged();
}

qreal ContextManager::fixtureDmxScale() const
{
    if (m_selectedFixtures.count() != 1)
        return 1.0;

    quint32 fxID = FixtureUtils::itemFixtureID(m_selectedFixtures.first());
    return qreal(m_monProps->fixtureDmxScale(fxID, 0, 0));
}

void ContextManager::setFixtureDmxScale(qreal scale)
{
    if (m_selectedFixtures.count() != 1)
        return;

    // A zero/negative scale would make pushPositionDelta()/pushRotationDelta()
    // divide by zero (or silently flip sign for a negative one, which invert
    // already covers) - clamp to a small positive minimum instead of allowing it.
    if (scale <= 0.0)
        scale = 0.01;

    quint32 fxID = FixtureUtils::itemFixtureID(m_selectedFixtures.first());
    float newScale = float(scale);
    if (qFuzzyCompare(newScale + 1.0f, m_monProps->fixtureDmxScale(fxID, 0, 0) + 1.0f))
        return;

    m_monProps->setFixtureDmxScale(fxID, 0, 0, newScale);
    m_doc->setModified();
    refreshFixtureDmxTransform(fxID);
    emit fixtureDmxScaleChanged();
}

void ContextManager::refreshFixtureDmxTransform(quint32 fxID)
{
    // The setting change alone doesn't touch DMX, so the cached delta (which
    // pre-dates the change) must be dropped rather than reused - see
    // cachedPositionDelta()/cachedRotationDelta()'s own docs for why they
    // can't simply be re-derived from the fixture's channel values on every
    // call instead; here, unlike that general case, we know for certain the
    // cache is now stale, so unconditionally dropping it is correct.
    m_fixturePositionDeltaCache.remove(fxID);
    m_fixtureRotationDeltaCache.remove(fxID);

    Fixture *fixture = m_doc->fixture(fxID);
    if (fixture == nullptr)
        return;

    quint32 itemID = FixtureUtils::fixtureItemID(fxID, 0, 0);

    bool hasPosition = fixture->channelNumber(QLCChannel::PositionX, QLCChannel::MSB) != QLCChannel::invalid() ||
                       fixture->channelNumber(QLCChannel::PositionY, QLCChannel::MSB) != QLCChannel::invalid() ||
                       fixture->channelNumber(QLCChannel::PositionZ, QLCChannel::MSB) != QLCChannel::invalid();
    bool hasRotation = fixture->channelNumber(QLCChannel::RotationX, QLCChannel::MSB) != QLCChannel::invalid() ||
                       fixture->channelNumber(QLCChannel::RotationY, QLCChannel::MSB) != QLCChannel::invalid() ||
                       fixture->channelNumber(QLCChannel::RotationZ, QLCChannel::MSB) != QLCChannel::invalid();

    if (hasPosition)
    {
        QVector3D newPos = FixtureUtils::gridCenterPosition(m_monProps) +
                           (cachedPositionDelta(fxID, fixture) * 1000.0f);
        if (m_2DView->isEnabled())
            m_2DView->updateFixturePosition(itemID, newPos);
        if (m_3DView->isEnabled())
            m_3DView->updateFixturePosition(itemID, newPos);
    }

    if (hasRotation)
    {
        QVector3D newRot = cachedRotationDelta(fxID, fixture);
        if (m_2DView->isEnabled())
            m_2DView->updateFixtureRotation(itemID, newRot);
        if (m_3DView->isEnabled())
            m_3DView->updateFixtureRotation(itemID, newRot);
    }
}

void ContextManager::setFixtureGroupSelection(quint32 id, bool enable, bool isUniverse)
{
    setBatchSelection(true);
    if (isUniverse)
    {
        for (Fixture *fixture : m_doc->fixtures())
        {
            if (fixture->universe() == id)
            {
                for (quint32 &subID : m_monProps->fixtureIDList(fixture->id()))
                {
                    quint16 headIndex = m_monProps->fixtureHeadIndex(subID);
                    quint16 linkedIndex = m_monProps->fixtureLinkedIndex(subID);
                    quint32 itemID = FixtureUtils::fixtureItemID(fixture->id(), headIndex, linkedIndex);
                    setFixtureSelection(itemID, -1, enable);
                }
            }
        }
    }
    else
    {
        FixtureGroup *group = m_doc->fixtureGroup(id);
        if (group == nullptr)
        {
            setBatchSelection(false);
            return;
        }

        for (quint32 &fxID : group->fixtureList())
        {
            Fixture *fixture = m_doc->fixture(fxID);
            if (fixture == nullptr)
                continue;

            for (quint32 &subID : m_monProps->fixtureIDList(fxID))
            {
                quint16 headIndex = m_monProps->fixtureHeadIndex(subID);
                quint16 linkedIndex = m_monProps->fixtureLinkedIndex(subID);
                quint32 itemID = FixtureUtils::fixtureItemID(fxID, headIndex, linkedIndex);
                setFixtureSelection(itemID, -1, enable);
            }
        }
    }
    setBatchSelection(false);
}

bool ContextManager::isGroupFullySelected(quint32 id) const
{
    FixtureGroup *group = m_doc->fixtureGroup(id);
    return FixtureUtils::isGroupFullySelected(group, m_monProps, m_selectedFixtures);
}

void ContextManager::selectNextFixtureGroup()
{
    QList<FixtureGroup *> groups = m_doc->fixtureGroups();
    if (groups.isEmpty())
        return;

    /* Find the index of the currently selected group, if any */
    int currentIndex = -1;
    for (int i = 0; i < groups.count(); i++)
    {
        if (groups.at(i)->id() == m_currentFixtureGroupID)
        {
            currentIndex = i;
            break;
        }
    }

    /* Move on to the next group, cycling back to the first one */
    int nextIndex = (currentIndex + 1) % groups.count();
    quint32 nextGroupID = groups.at(nextIndex)->id();

    /* Clear the current selection before selecting the new group */
    resetFixtureSelection();

    m_currentFixtureGroupID = nextGroupID;
    setFixtureGroupSelection(nextGroupID, true, false);
}

void ContextManager::slotNewFixtureCreated(quint32 fxID, qreal x, qreal y, qreal z)
{
    if (m_doc->loadStatus() == Doc::Loading)
        return;

    qDebug() << "[ContextManager] New fixture created" << fxID;

    if (m_uniGridView->isEnabled())
        m_monProps->setFixturePosition(fxID, 0, 0, QVector3D(0, 0, 0));
    if (m_DMXView->isEnabled())
        m_DMXView->createFixtureItem(fxID);
    if (m_2DView->isEnabled())
        m_2DView->createFixtureItems(fxID, QVector3D(x, y, z), false);
    if (m_3DView->isEnabled())
        m_3DView->createFixtureItems(fxID, QVector3D(x, y, z), false);
}

void ContextManager::slotFixtureDeleted(quint32 itemID)
{
    if (m_doc->loadStatus() == Doc::Loading)
        return;

    qDebug() << "[ContextManager] Removing item" << itemID;

    if (m_DMXView->isEnabled())
        m_DMXView->removeFixtureItem(FixtureUtils::itemFixtureID(itemID));
    if (m_2DView->isEnabled())
        m_2DView->removeFixtureItem(itemID);
    if (m_3DView->isEnabled())
        m_3DView->removeFixtureItem(itemID);
}

void ContextManager::slotFixtureFlagsChanged(quint32 itemID, quint32 flags)
{
    if (m_DMXView->isEnabled())
        m_DMXView->setFixtureFlags(itemID, flags);
    if (m_2DView->isEnabled())
        m_2DView->setFixtureFlags(itemID, flags);
    if (m_3DView->isEnabled())
        m_3DView->setFixtureFlags(itemID, flags);
}

void ContextManager::slotChannelValueChanged(quint32 fxID, quint32 channel, quint8 value)
{
    if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
        setDumpValue(fxID, channel, uchar(value));
    else
        m_functionManager->setChannelValue(fxID, channel, uchar(value));
}

void ContextManager::setColorValue(QColor col, QColor wauv)
{
    setChannelValueByType((int)QLCChannel::Red, col.red());
    setChannelValueByType((int)QLCChannel::Green, col.green());
    setChannelValueByType((int)QLCChannel::Blue, col.blue());

    setChannelValueByType((int)QLCChannel::White, wauv.red());
    setChannelValueByType((int)QLCChannel::Amber, wauv.green());
    setChannelValueByType((int)QLCChannel::UV, wauv.blue());

    QColor cmykColor = col.toCmyk();
    setChannelValueByType((int)QLCChannel::Cyan, cmykColor.cyan());
    setChannelValueByType((int)QLCChannel::Magenta, cmykColor.magenta());
    setChannelValueByType((int)QLCChannel::Yellow, cmykColor.yellow());
}

void ContextManager::setChannelValueByType(int type, int value, bool isRelative, quint32 channel)
{
    //qDebug() << "[setChannelValueByType] type:" << type << "value:" << value << "relative:" << isRelative << "channel:" << channel;
    QList<SceneValue> svList = m_channelsMap.values(type);
    for (SceneValue &sv : svList)
    {
        if (channel == UINT_MAX || channel == sv.channel)
        {
            uchar val = value;

            if (isRelative)
            {
                Fixture *fixture = m_doc->fixture(sv.fxi);
                if (fixture == nullptr)
                    continue;

                const QLCChannel *ch = fixture->channel(sv.channel);
                if (ch == nullptr)
                    continue;

                val = qBound(0, fixture->channelValueAt(sv.channel) + value, 255);
            }

            if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
                setDumpValue(sv.fxi, sv.channel, val);
            else
                m_functionManager->setChannelValue(sv.fxi, sv.channel, val);
        }
    }
}

void ContextManager::setPositionValue(int type, float degrees, bool isRelative)
{
    // list to keep track of the already processed Fixture IDs
    QList<quint32>fxIDs;
    QList<SceneValue> typeList = m_channelsMap.values(type);

    for (SceneValue &sv : typeList)
    {
        if (fxIDs.contains(sv.fxi) == true)
            continue;

        fxIDs.append(sv.fxi);

        Fixture *fixture = m_doc->fixture(sv.fxi);
        if (fixture == nullptr || fixture->fixtureMode() == nullptr)
            continue;

        QList<SceneValue> svList = fixture->positionToValues(type, degrees, isRelative);
        for (SceneValue &posSv : svList)
        {
            if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
                setDumpValue(posSv.fxi, posSv.channel, posSv.value);
            else
                m_functionManager->setChannelValue(posSv.fxi, posSv.channel, posSv.value);
        }
    }
}

void ContextManager::setPositionCenter()
{
    setChannelValueByType((int)QLCChannel::Pan, 127);
    setChannelValueByType((int)QLCChannel::Tilt, 127);
}

void ContextManager::setBeamDegrees(float degrees, bool isRelative)
{
    // list to keep track of the already processed Fixture IDs
    QList<quint32>fxIDs;
    QList<SceneValue> typeList = m_channelsMap.values(QLCChannel::Beam);

    for (SceneValue &sv : typeList)
    {
        if (fxIDs.contains(sv.fxi) == true)
            continue;

        fxIDs.append(sv.fxi);

        Fixture *fixture = m_doc->fixture(sv.fxi);
        if (fixture == nullptr || fixture->fixtureMode() == nullptr)
            continue;

        QList<SceneValue> svList = fixture->zoomToValues(degrees, isRelative);
        for (SceneValue &zSv : svList)
        {
            if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
                setDumpValue(zSv.fxi, zSv.channel, zSv.value);
            else
                m_functionManager->setChannelValue(zSv.fxi, zSv.channel, zSv.value);
        }
    }
}

void ContextManager::highlightFixtureSelection()
{
    setChannelValueByType((int)QLCChannel::Red, UCHAR_MAX);
    setChannelValueByType((int)QLCChannel::Green, UCHAR_MAX);
    setChannelValueByType((int)QLCChannel::Blue, UCHAR_MAX);
    //setChannelValueByType((int)QLCChannel::White, UCHAR_MAX);

    setChannelValueByType((int)QLCChannel::Intensity, UCHAR_MAX);

    // search for shutter open and lamp on
    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fxID = FixtureUtils::itemFixtureID(itemID);
        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture == nullptr)
            continue;

        for (quint32 i = 0; i < fixture->channels(); i++)
        {
            const QLCChannel *channel = fixture->channel(i);
            for (QLCCapability *cap : channel->capabilities())
            {
                if (cap->preset() == QLCCapability::ShutterOpen ||
                    cap->preset() == QLCCapability::LampOn)
                {
                    if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
                        setDumpValue(fxID, i, cap->middle());
                    else
                        m_functionManager->setChannelValue(fxID, i, cap->middle());
                    break;
                }
            }
        }
    }
}

void ContextManager::setChannelValues(QList<SceneValue> values)
{
    for (SceneValue &sv : values)
    {
        if (m_editingEnabled == false || m_functionManager->acceptsSceneValues() == false)
            setDumpValue(sv.fxi, sv.channel, sv.value);
        else
            m_functionManager->setChannelValue(sv.fxi, sv.channel, sv.value);
    }
}

void ContextManager::slotPresetChanged(const QLCChannel *channel, quint8 value)
{
    for (quint32 &itemID : m_selectedFixtures)
    {
        quint32 fixtureID = FixtureUtils::itemFixtureID(itemID);
        Fixture *fixture = m_doc->fixture(fixtureID);
        if (fixture == nullptr)
            continue;

        if (fixture->fixtureDef() != nullptr && fixture->fixtureMode() != nullptr)
        {
            quint32 chIdx = fixture->fixtureMode()->channelNumber((QLCChannel *)channel);
            if (chIdx != QLCChannel::invalid())
                setChannelValueByType((int)channel->group(), value, false, chIdx);
        }
    }
}

void ContextManager::slotSimpleDeskValueChanged(quint32 fxID, quint32 channel, quint8 value)
{
    if (m_editingEnabled == false)
        setDumpValue(fxID, channel, uchar(value), false);
}

// Used by slotUniverseWritten() below to compare a single axis of a cached
// position/rotation delta against the value freshly re-derived from the
// fixture's own channels. Axes the fixture has no channel for must be
// skipped entirely rather than compared as 0 vs 0 - FixtureUtils::
// fixturePositionDelta()/fixtureRotationDelta() report 0 for those, but the
// cached delta can be non-zero on such an axis (e.g. a fixture with only a
// PositionX channel, dragged diagonally still accumulates a Z component in
// the cache), which would otherwise look like a spurious external change on
// every single universe write and defeat the cache.
static bool axisDeltaChanged(Fixture *fixture, QLCChannel::Group group,
                              float cachedValue, float freshValue, float epsilon)
{
    if (fixture->channelNumber(group, QLCChannel::MSB) == QLCChannel::invalid())
        return false;

    return qAbs(freshValue - cachedValue) > epsilon;
}

void ContextManager::slotUniverseWritten(quint32 idx, const QByteArray &ua)
{
    for (Fixture *fixture : m_doc->fixtures())
    {
        if (fixture->universe() != idx)
            continue;

        QByteArray prevValues;
        prevValues.append(fixture->channelValues());

        if (fixture->setChannelValues(ua) == true)
        {
            // A Scene, Simple Desk or Chaser (or another console entirely) may
            // have just written this fixture's position/rotation channels
            // behind our back. Our own pushPositionDelta()/pushRotationDelta()
            // writes land here too, once Doc's processing tick catches up, so
            // only drop a cache entry when the fixture's actual channel values
            // no longer match what we last told it to be - comparing against
            // an exact re-derivation (rather than unconditionally dropping on
            // every write) avoids immediately invalidating the entry a drag
            // step just wrote, which would reintroduce the stale-delta bug
            // this cache was added to fix. The epsilons are a few DMX raw
            // steps wide (see positionRawFromDelta()/rotationRawFromDelta()),
            // comfortably above float round-trip quantization noise between a
            // pushed delta and its channel-value re-derivation. Comparison is
            // done per-axis (see axisDeltaChanged()) so an axis the fixture
            // has no channel for - always 0 from the fresh re-derivation, but
            // possibly non-zero in the cache - never looks like an external
            // change.
            //
            // Known remaining race, not addressed here: an external write
            // landing in the narrow window between this class's own push and
            // Doc's processing tick catching up can be read as external and
            // drop that window's increment. This is bounded (at most the
            // increments accumulated within one processing tick) and doesn't
            // compound like the original un-cached bug did.
            quint32 fxID = fixture->id();

            // The epsilons below are a few DMX raw steps wide *before* any
            // per-fixture DMX scale is applied - since fixturePositionDelta()/
            // fixtureRotationDelta() now multiply their result by that same
            // scale (see MonitorProperties::fixtureDmxScale()), the epsilon
            // must be scaled by the same factor here or a fixture with a
            // scale far from 1.0 would compare its (now much smaller or
            // larger) view-space quantization noise against an epsilon sized
            // for scale 1.0, either false-triggering on noise or missing a
            // real external change.
            float dmxScale = m_monProps->fixtureDmxScale(fxID, 0, 0);

            QHash<quint32, QVector3D>::iterator posIt = m_fixturePositionDeltaCache.find(fxID);
            if (posIt != m_fixturePositionDeltaCache.end())
            {
                QVector3D fresh = FixtureUtils::fixturePositionDelta(fixture, m_monProps);
                QVector3D cached = posIt.value();
                float posEpsilon = 0.0005f * qAbs(dmxScale);
                if (axisDeltaChanged(fixture, QLCChannel::PositionX, cached.x(), fresh.x(), posEpsilon) ||
                    axisDeltaChanged(fixture, QLCChannel::PositionY, cached.y(), fresh.y(), posEpsilon) ||
                    axisDeltaChanged(fixture, QLCChannel::PositionZ, cached.z(), fresh.z(), posEpsilon))
                {
                    m_fixturePositionDeltaCache.erase(posIt);
                }
            }

            QHash<quint32, QVector3D>::iterator rotIt = m_fixtureRotationDeltaCache.find(fxID);
            if (rotIt != m_fixtureRotationDeltaCache.end())
            {
                QVector3D fresh = FixtureUtils::fixtureRotationDelta(fixture, m_monProps);
                QVector3D cached = rotIt.value();
                float rotEpsilon = 0.05f * qAbs(dmxScale);
                if (axisDeltaChanged(fixture, QLCChannel::RotationX, cached.x(), fresh.x(), rotEpsilon) ||
                    axisDeltaChanged(fixture, QLCChannel::RotationY, cached.y(), fresh.y(), rotEpsilon) ||
                    axisDeltaChanged(fixture, QLCChannel::RotationZ, cached.z(), fresh.z(), rotEpsilon))
                {
                    m_fixtureRotationDeltaCache.erase(rotIt);
                }
            }

            if (m_DMXView->isEnabled())
                m_DMXView->updateFixture(fixture);
            if (m_2DView->isEnabled())
                m_2DView->updateFixture(fixture, prevValues);
            if (m_3DView->isEnabled())
                m_3DView->updateFixture(fixture, prevValues);
        }
    }
}

void ContextManager::slotFunctionEditingChanged(bool status)
{
    if (status == m_editingEnabled)
        return;

    resetFixtureSelection();
    m_editingEnabled = status;
}

/*********************************************************************
 * DMX channels dump
 *********************************************************************/

void ContextManager::setDumpValue(quint32 fxID, quint32 channel, uchar value, bool output)
{
    QVariant currentVal, newVal;
    SceneValue sValue(fxID, channel, value);
    int valIndex = m_dumpValues.indexOf(sValue);
    uchar currDmxValue = valIndex >= 0 ? m_dumpValues.at(valIndex).value : 0;
    currentVal.setValue(SceneValue(fxID, channel, currDmxValue));
    newVal.setValue(sValue);

    //if (currentVal != newVal || value != currDmxValue)
    {
        if (output)
        {
            Tardis::instance()->enqueueAction(Tardis::FixtureSetDumpValue, 0, currentVal, newVal);
            if (m_source)
                m_source->set(fxID, channel, value);
        }

        if (valIndex >= 0)
        {
            m_dumpValues.replace(valIndex, sValue);
        }
        else
        {
            m_dumpValues.append(sValue);
            emit dumpValuesCountChanged();

            const QLCChannel *ch = m_doc->fixture(fxID)->channel(channel);
            if (ch != nullptr)
            {
                if (ch->group() == QLCChannel::Intensity)
                {
                    if (ch->colour() == QLCChannel::NoColour)
                        m_dumpChannelMask |= App::DimmerType;
                    else
                        m_dumpChannelMask |= App::ColorType;
                }
                else
                {
                    m_dumpChannelMask |= (1 << ch->group());
                }
                emit dumpChannelMaskChanged();
            }
        }

        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture)
            fixture->checkAlias(channel, value);
    }
}

void ContextManager::unsetDumpValue(quint32 fxID, quint32 channel)
{
    SceneValue sValue(fxID, channel, 0);
    int valIndex = m_dumpValues.indexOf(sValue);

    if (valIndex >= 0)
    {
        m_dumpValues.removeAt(valIndex);
        emit dumpValuesCountChanged();
    }
}

QList<quint32> ContextManager::selectedFixtureIDList() const
{
    QList<quint32> fxIDList;

    for (quint32 itemID : m_selectedFixtures)
    {
        quint32 fixtureID = FixtureUtils::itemFixtureID(itemID);
        if (fxIDList.contains(fixtureID) == false)
            fxIDList.append(fixtureID);
    }

    return fxIDList;
}

int ContextManager::dumpValuesCount() const
{
    int i = 0;
    QList<quint32> fxIDList = selectedFixtureIDList();

    if (fxIDList.isEmpty())
        return m_dumpValues.count();

    for (SceneValue sv : m_dumpValues)
        if (fxIDList.contains(sv.fxi))
            i++;

    return i;
}

int ContextManager::dumpChannelMask() const
{
    return m_dumpChannelMask;
}

void ContextManager::dumpDmxChannels(quint32 channelMask, QString sceneName, int sceneID, bool allChannels, bool nonZeroOnly)
{
    m_functionManager->dumpDmxValues(m_dumpValues, allChannels ? QList<quint32>() : selectedFixtureIDList(), channelMask,
                                     sceneName, sceneID == -1 ? Function::invalidId() : sceneID, nonZeroOnly);
}

void ContextManager::resetDumpValues()
{
    QVariantList oldValues;
    for (const SceneValue &sv : m_dumpValues)
        oldValues.append(QVariant::fromValue(sv));

    if (!oldValues.isEmpty())
    {
        Tardis::instance()->enqueueAction(Tardis::FixtureResetDumpValues, 0,
                              oldValues, QVariantList());
    }

    for (SceneValue &sv : m_dumpValues)
        m_source->unset(sv.fxi, sv.channel);

    m_source->unsetAll();

    m_dumpValues.clear();
    emit dumpValuesCountChanged();

    m_dumpChannelMask = 0;
    emit dumpChannelMaskChanged();
}
