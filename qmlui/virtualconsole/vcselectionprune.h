/*
  Q Light Controller Plus
  vcselectionprune.h

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

#ifndef VCSELECTIONPRUNE_H
#define VCSELECTIONPRUNE_H

#include <QList>
#include <QMap>
#include <QSet>

/**
 * Drops the entries of $selection whose widget id is in $deletedIds and returns the ids it
 * removed. Every other (surviving) entry stays selected.
 *
 * VirtualConsole::deleteVCWidgets() uses this instead of clearing the whole selection: a
 * delete that does not come from the selection itself (a Control API client deleting one
 * widget while the operator has others selected) used to empty the selection map while the
 * surviving widgets kept isEditing == true - and nothing ever reset it, because leaving edit
 * mode only clears the widgets still in the map, so those widgets ignored MIDI and keyboard
 * input until they were selected again.
 */
template <typename T>
QList<quint32> vcPruneDeletedFromSelection(QMap<quint32, T> &selection, const QSet<quint32> &deletedIds)
{
    QList<quint32> removed;
    for (auto it = selection.begin(); it != selection.end(); )
    {
        if (deletedIds.contains(it.key()))
        {
            removed.append(it.key());
            it = selection.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return removed;
}

#endif
