/*
  Q Light Controller Plus
  diagnostics_resource.h

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

// Shared control/dialog IDs between qmlui.rc (the dialog templates) and
// diagnostics.cpp (the code driving them). Kept in its own tiny header,
// included by both, so the two never drift apart.
//
// Both diagnostic dialogs (freeze and crash) share the same control layout
// and IDs - only the dialog ID, caption and intro text differ - so one
// dialog procedure (Diagnostics::showReportDialog) drives either of them.
#ifndef DIAGNOSTICS_RESOURCE_H
#define DIAGNOSTICS_RESOURCE_H

#define IDD_FREEZE_DIALOG   101
#define IDD_CRASH_DIALOG    102
#define IDC_DIAG_EDIT       1001
#define IDC_DIAG_COPY       1002
#define IDC_STATIC          -1

#endif // DIAGNOSTICS_RESOURCE_H
