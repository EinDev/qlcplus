/*
  Q Light Controller Plus
  freezewatchdog_resource.h

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

// Shared control/dialog IDs between qmlui.rc (the dialog template) and
// freezewatchdog.cpp (the code driving it). Kept in its own tiny header,
// included by both, so the two never drift apart.
#ifndef FREEZEWATCHDOG_RESOURCE_H
#define FREEZEWATCHDOG_RESOURCE_H

#define IDD_FREEZE_DIALOG   101
#define IDC_FREEZE_EDIT     1001
#define IDC_FREEZE_COPY     1002
#define IDC_STATIC          -1

#endif // FREEZEWATCHDOG_RESOURCE_H
