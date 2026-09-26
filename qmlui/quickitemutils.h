/*
  Q Light Controller Plus
  quickitemutils.h

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

#ifndef QUICKITEMUTILS_H
#define QUICKITEMUTILS_H

#include <QQuickItem>
#include <QString>

/**
 * Find the QQuickItem with the given objectName that belongs to the
 * top-level tab currently shown.
 *
 * Several tabs own an item with the same objectName (Fixtures & Functions
 * and the Show Manager both have a "funcRightPanel" and a "bottomPanelItem"),
 * and since MainView.qml keeps every visited tab's Loader alive instead of
 * destroying it on a switch, a plain findChild() from the root can return
 * the one living in a hidden tab. This picks the candidate whose parent
 * item (the tab's root, whose visibility follows its Loader) is effectively
 * visible. The candidate's own visibility is deliberately not tested: the
 * bottom panels start hidden and are shown on demand.
 *
 * Falls back to the first candidate when none is in a visible tab, which
 * keeps the historic behaviour for calls made while no tab is shown.
 */
static inline QQuickItem *findVisibleContextItem(QQuickItem *root, const QString &objectName)
{
    if (root == nullptr)
        return nullptr;

    QQuickItem *fallback = nullptr;
    const QList<QQuickItem *> candidates = root->findChildren<QQuickItem *>(objectName);
    for (QQuickItem *item : candidates)
    {
        QQuickItem *parent = item->parentItem();
        if (parent != nullptr && parent->isVisible())
            return item;
        if (fallback == nullptr)
            fallback = item;
    }
    return fallback;
}

#endif // QUICKITEMUTILS_H
