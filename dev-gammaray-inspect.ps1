<#
.SYNOPSIS
  Read/drive a running GammaRay client window via Windows UI Automation, so its object
  tree and property tables can be queried as structured text instead of screenshots.

.DESCRIPTION
  Companion to the GammaRay build at C:\gr (built from source in C:\gr\src because MSYS2
  only packages gammaray for ucrt64/clang64, not the mingw64 ABI this project's Qt6 uses -
  see the session that set this up). Launch GammaRay first:

    C:\gr\install\bin\gammaray.exe C:\qlcplus\qlcplus5.exe

  Then dot-source this file to load the functions below into your shell:

    . .\dev-gammaray-inspect.ps1
    Connect-GammaRay
    Get-GammaRayToolList
    Select-GammaRayTool -Name Objects
    Select-GammaRayTreeNode -Name TreeModel
    Get-GammaRayProperties | Format-Table -AutoSize

  Qt's Windows accessibility bridge exposes GammaRay's own Widgets UI as a real UIA tree,
  which is what makes this possible - it's reading GammaRay's rendered widget state, not
  reaching into the target process directly.

  KNOWN LIMITS (verified in the session that wrote this):
   - GammaRay's object/QML tree view is virtualized like this project's own TreeModel
     (see CLAUDE.md) - Get-GammaRayTree / Select-GammaRayTreeNode only see rows currently
     scrolled into view and expanded, not the whole model. Expand ancestor nodes first.
   - Get-GammaRayProperties assumes the current tool tab shows a flat table under column
     headers (true for Objects/Meta Objects/Meta Types/...). Tools with a different layout
     (Quick Scenes' live preview, Qt3D Inspector's 3D view) render custom content with no
     UIA tree behind it - fall back to Save-GammaRayScreenshot + reading the image instead.
   - Editing a property value live has NOT been tested/implemented here - Qt's tree views
     need double-click-to-edit, which would need ValuePattern or synthesized input; treat
     this script as read/navigate only until that's proven out.
   - A blank Class in Get-GammaRayProperties is not a parsing failure - GammaRay only
     shows the declaring class when it differs from the selected object's own runtime
     type (e.g. QApplication's own properties, or a QQuickItem's base properties on a
     plain QQuickItem, show blank; a property inherited from a different ancestor shows
     that ancestor's name).
   - Calling FindAll right after selecting a node can throw a transient
     "Unbekannter Fehler"/UnknownError COMException while GammaRay's property table is
     still repopulating - retry after a short Start-Sleep rather than treating it as fatal.
#>

Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes

$script:GammaRayWindow = $null

function Connect-GammaRay {
    <#
    .SYNOPSIS
      Locate the running gammaray-client window and cache it for subsequent calls.
    .PARAMETER TitlePattern
      Wildcard match against the window title. Default matches GammaRay's standard
      "GammaRay (<target app display name>)" title.
    #>
    param(
        [string]$TitlePattern = "GammaRay*"
    )
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $windows = $root.FindAll(
        [System.Windows.Automation.TreeScope]::Children,
        [System.Windows.Automation.Condition]::TrueCondition)
    $match = $windows | Where-Object { $_.Current.Name -like $TitlePattern } | Select-Object -First 1
    if (-not $match) {
        throw "No window matching '$TitlePattern' found. Is gammaray-client.exe running? (launch via C:\gr\install\bin\gammaray.exe <target.exe>)"
    }
    $script:GammaRayWindow = $match
    Write-Output "Connected: $($match.Current.Name)"
    return $match
}

function Get-GRWindow {
    if (-not $script:GammaRayWindow) { Connect-GammaRay | Out-Null }
    return $script:GammaRayWindow
}

function Get-GammaRayToolList {
    <#
    .SYNOPSIS
      List GammaRay's left-hand sidebar tool tabs (Objects, Quick Scenes, Meta Objects, ...).
    #>
    $win = Get-GRWindow
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::ListItem)
    $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond) |
        ForEach-Object { $_.Current.Name } |
        Where-Object { $_ -and $_.Trim() -ne "" }
}

function Select-GammaRayTool {
    <#
    .SYNOPSIS
      Switch GammaRay's main view to the named sidebar tool tab.
    .EXAMPLE
      Select-GammaRayTool -Name Objects
    #>
    param([Parameter(Mandatory)][string]$Name)
    $win = Get-GRWindow
    $typeCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::ListItem)
    $nameCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty, $Name)
    $andCond = New-Object System.Windows.Automation.AndCondition($typeCond, $nameCond)
    $item = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $andCond)
    if (-not $item) {
        throw "No tool tab named '$Name'. Available: $((Get-GammaRayToolList) -join ', ')"
    }
    $pattern = $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    $pattern.Select()
    Start-Sleep -Milliseconds 400
}

function Get-GammaRayTree {
    <#
    .SYNOPSIS
      Dump all currently-visible TreeItem elements (object/QML tree nodes AND property
      table rows both surface as TreeItem under UIA), tagged Left/Right pane by screen
      position so callers can tell the object tree apart from the property table.
    .NOTES
      Only rows scrolled into view / expanded are visible - see the virtualization note
      in this file's top-level help. Qt's accessibility layer does not create an element
      at all for an empty cell (e.g. a row with no Class value just has 3 cells, not 4
      with one blank) - Get-GammaRayProperties therefore matches cells to columns by X
      position, not by counting, and callers reading this raw list should do the same.
    #>
    $win = Get-GRWindow
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::TreeItem)
    $items = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)
    $winRect = $win.Current.BoundingRectangle
    $midX = $winRect.X + ($winRect.Width / 2)
    foreach ($el in $items) {
        $rect = $el.Current.BoundingRectangle
        [PSCustomObject]@{
            Name    = $el.Current.Name
            X       = $rect.X
            Y       = $rect.Y
            Pane    = if ($rect.X -lt $midX) { "Left" } else { "Right" }
            Element = $el
        }
    }
}

function Select-GammaRayTreeNode {
    <#
    .SYNOPSIS
      Find a visible node by exact name in the left-hand object/QML tree and select it
      (populating the right-hand property table for Get-GammaRayProperties).
    .PARAMETER Expand
      Also expand the node's children before selecting.
    #>
    param(
        [Parameter(Mandatory)][string]$Name,
        [switch]$Expand
    )
    $node = Get-GammaRayTree | Where-Object { $_.Pane -eq "Left" -and $_.Name -eq $Name } | Select-Object -First 1
    if (-not $node) {
        throw "No visible tree node named '$Name' in the left pane. It may need an ancestor expanded/scrolled into view first."
    }
    $el = $node.Element
    if ($Expand) {
        $expandPattern = $null
        if ($el.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$expandPattern)) {
            $expandPattern.Expand()
        }
    }
    $sel = $el.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    $sel.Select()
    Start-Sleep -Milliseconds 300
}

function Get-GammaRayTable {
    <#
    .SYNOPSIS
      Read whatever row/column table the currently selected tool tab shows, as objects
      keyed by the table's own column headers.
    .PARAMETER Pane
      Which half of the window to read headers/cells from. 'Right' (default) is for
      split-pane tools like Objects/Meta Objects/Meta Types, which have a left-hand
      object/QML tree (with its own single-column header, e.g. "Object") alongside the
      right-hand property table - only the right pane is the table you usually want.
      'All' is for tools with one full-width table and no separate left-hand tree, like
      Messages.
    .NOTES
      Cells are matched to columns by nearest X position, not by counting cells per row -
      Qt's accessibility layer omits an element entirely for an empty cell, so a row can
      have fewer visible cells than there are columns (e.g. no Class value). Rows are
      grouped by Y with a tolerance, since same-row cells share (near-)identical Y.
    #>
    param(
        [ValidateSet('Left', 'Right', 'All')]
        [string]$Pane = 'Right'
    )
    $win = Get-GRWindow
    $winRect = $win.Current.BoundingRectangle
    $midX = $winRect.X + ($winRect.Width / 2)

    $headerCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Header)
    $headers = @($win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $headerCond) |
        Where-Object {
            $hx = $_.Current.BoundingRectangle.X
            switch ($Pane) {
                'Left'  { $hx -lt $midX }
                'Right' { $hx -ge $midX }
                'All'   { $true }
            }
        } |
        ForEach-Object {
            [PSCustomObject]@{ Name = $_.Current.Name; X = $_.Current.BoundingRectangle.X }
        } | Sort-Object X)
    if ($headers.Count -eq 0) {
        throw "No column headers found for Pane='$Pane' - is a table-based tool tab selected (e.g. Select-GammaRayTool -Name Objects)?"
    }

    $items = @(Get-GammaRayTree | Where-Object {
            switch ($Pane) {
                'Left'  { $_.Pane -eq 'Left' }
                'Right' { $_.Pane -eq 'Right' }
                'All'   { $true }
            }
        } | Sort-Object Y, X)

    # Bucket cells into rows: a new row starts whenever Y jumps more than half a
    # typical row height from the previous cell (same-row cells share ~identical Y).
    $rowGroups = @()
    $current = @()
    $prevY = $null
    foreach ($item in $items) {
        if ($null -ne $prevY -and [Math]::Abs($item.Y - $prevY) -gt 8) {
            $rowGroups += , $current
            $current = @()
        }
        $current += $item
        $prevY = $item.Y
    }
    if ($current.Count -gt 0) { $rowGroups += , $current }

    $rows = @()
    foreach ($cells in $rowGroups) {
        $row = [ordered]@{}
        foreach ($h in $headers) { $row[$h.Name] = "" }
        foreach ($cell in $cells) {
            $nearest = $headers | Sort-Object { [Math]::Abs($_.X - $cell.X) } | Select-Object -First 1
            $row[$nearest.Name] = $cell.Name
        }
        $rows += [PSCustomObject]$row
    }
    return $rows
}

function Get-GammaRayProperties {
    <#
    .SYNOPSIS
      Read the right-hand property table for whatever's currently selected (Property/
      Value/Type/Class for the Objects tool, but adapts to whatever headers the active
      tool tab actually shows). Thin wrapper over Get-GammaRayTable -Pane Right.
    #>
    Get-GammaRayTable -Pane Right
}

function Get-GammaRayMessages {
    <#
    .SYNOPSIS
      Read the Messages tool (qDebug/qWarning/qCritical output, including QML console
      errors) as structured objects: Time, Message, Category, Function, Source. Selects
      the Messages tool tab first.
    .EXAMPLE
      Get-GammaRayMessages | Where-Object Message -like '*FunctionManagerFlatDelegate*'
    .NOTES
      Same virtualization caveat as everything else here (see top-level help) - only
      rows currently scrolled into view come back. The message list is a single
      full-width table with no separate left-hand tree, hence Pane 'All'.
    #>
    Select-GammaRayTool -Name Messages
    Get-GammaRayTable -Pane All
}

function Save-GammaRayScreenshot {
    <#
    .SYNOPSIS
      Fallback for views with no UIA tree behind them (Quick Scenes live preview, Qt3D
      Inspector) - captures just the GammaRay window's own screen region, not the full
      desktop, so you're not hunting for it in a multi-monitor screenshot afterwards.
    #>
    param(
        [string]$Path = "$env:TEMP\gammaray-window.png"
    )
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $win = Get-GRWindow
    $r = $win.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap ([int]$r.Width), ([int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output $Path
}
