// UI-related resources for Write

const char* mainWindowSVG = R"#(
<svg class="window" layout="box">
  <!-- rect fill="white" box-anchor="fill" width="20" height="20"/ -->
  <g class="window-layout" box-anchor="fill" layout="flex" flex-direction="column">
    <rect id="ios-statusbar-bg" class="toolbar" display="none" box-anchor="hfill" width="20" height="20"/>
    <g id="main-toolbar-container" box-anchor="hfill" layout="box">
    </g>
    <g id="pen-toolbar-container" box-anchor="hfill" layout="box">
    </g>

    <g id="main-container" box-anchor="fill" layout="box">
      <g class="sub-window-layout" box-anchor="fill" layout="flex" flex-direction="row">
        <g id="bookmark-panel" display="none" box-anchor="vfill" layout="flex" flex-direction="column">
          <g class="panel-header" box-anchor="hfill" layout="box">
            <rect class="background" box-anchor="fill" width="20" height="20"/>
            <!-- text display="none" class="title panel-title" box-anchor="left" margin="10 10">Bookmarks</text -->
            <g id="bookmarks-combo-container" box-anchor="left" layout="box"></g>
            <g id="bookmarks-toolbar-container" box-anchor="right" layout="box"></g>
          </g>
          <rect class="hrule title background" box-anchor="hfill" width="20" height="2"/>
          <g id="bookmark-container" box-anchor="fill" layout="box">
            <rect id="bookmark-split-sizer" fill="none" box-anchor="vfill" width="320" height="20"/>
          </g>
        </g>
        <rect id="bookmark-splitter" class="background splitter" display="none" box-anchor="vfill" width="10" height="10"/>

        <g id="clippings-panel" display="none" box-anchor="vfill" layout="flex" flex-direction="column">
          <g class="panel-header" box-anchor="hfill" layout="box">
            <rect class="background" box-anchor="fill" width="20" height="20"/>
            <text class="title panel-title" box-anchor="left" margin="10 10">Clippings</text>
            <g id="clippings-toolbar-container" box-anchor="right" layout="box"></g>
          </g>
          <rect class="hrule title background" box-anchor="hfill" width="20" height="2"/>
          <g id="clippings-container" box-anchor="fill" layout="box">
            <rect id="clippings-split-sizer" fill="none" box-anchor="vfill" width="320" height="20"/>
          </g>
        </g>
        <rect id="clippings-splitter" class="background splitter" display="none" box-anchor="vfill" width="10" height="10"/>

        <g id="scribble-split-layout" box-anchor="fill" layout="flex" flex-direction="column">
          <g id="scribble-focus-container" box-anchor="fill" layout="flex" flex-direction="column">
            <rect id="scribble-focus" class="hrule title background" box-anchor="hfill" width="20" height="2"/>
            <g id="scribble-container" box-anchor="fill" layout="box">
            </g>
          </g>
          <!-- wrap <rect> in <g> so that we don't have to reset size when changing split direction -->
          <g id="scribble-splitter" class="splitter" display="none" box-anchor="hfill">
            <rect class="background" width="10" height="10"/>
          </g>
          <!-- note the use of hfill here, necessary for sizer to work! -->
          <g id="scribble-focus-container-2" display="none" box-anchor="hfill" layout="flex" flex-direction="column">
            <rect id="scribble-focus-2" class="hrule title background inactive" box-anchor="hfill" width="20" height="2"/>
            <g id="scribble-container-2" box-anchor="fill" layout="box">
              <rect id="scribble-split-sizer" fill="none" box-anchor="hfill" width="20" height="400"/>
            </g>
          </g>
        </g>

      </g>  <!-- end sub-window-layout -->
      <!-- overlay widget for dragging selection will be inserted here -->
    </g>  <!-- end main-container -->
    <g id="notify-toolbar-container" box-anchor="hfill" layout="box">
    </g>
  </g>
</svg>
)#";

// previous dimensions were 60x60
const char* scribbleScrollerSVG = R"#(
<g class="scribble-scroller" box-anchor="right top">
  <use class="icon" width="40" height="40" xlink:href="icons/ic_scroll_handle.svg" />
</g>
)#";

const char* clippingsDelSVG = R"#(
  <g>
    <rect class="toolbar" fill-opacity="0.75" width="52" height="52"/>
    <use class="icon" width="52" height="52" xlink:href="icons/ic_menu_discard.svg" />
  </g>
)#";

// note that "exclude" attribute must be lower-case
const char* prefInfoXML = R"#(
<?xml version="1.0"?>
<map>
  <pref name="penButtonMode" type="int" level="1" group="Input"
      enum="None;Pan;Erase;Select;Insert space"
      enumvals="12;11;13;17;24"
      title="Pen button tool" description="Tool activated by pen button" />
  <pref name="mouseMode" type="int" level="1" group="Input" exclude="mobile"
      enum="Ignore;Pan;Draw"
      title="Mouse input mode" description="Action for mouse input" />
  <pref name="singleTouchMode" type="int" level="1" group="Input"
      enum="Ignore;Pan;Draw;Erase;Select;Insert space"
      enumvals="0;1;2;13;17;24"
      title="Single-touch mode" description="Action for single-touch input" />
  <pref name="multiTouchMode" type="int" level="1" group="Input"
      enum="Ignore;Pan;Pan and zoom"
      title="Multi-touch mode" description="Action for multi-touch input" />
  <pref name="panFromEdge" type="bool" group="Input"
      title="Pan from edge when drawing" description="Swipe in from edge of screen to pan" />
  <pref name="dropFirstPenPoint" type="bool" group="Input"
      title="Ignore first point from pen" description="First point is bad with some devices" exclude="ios" />
  <pref name="palmThreshold" type="float" min="0" max="1000" step="1" group="Input" exclude="desktop"
      title="Palm rejection threshold" description="0 to disable" />
  <!-- pref name="pressureScale" type="float" min="0" max="100" step="0.1" group="Input"
      title="Pen pressure scaling" description="Default is 1" / -->
  <pref name="inputSmoothing" type="int" min="0" max="100" group="Input"
      title="Smoothing" description="0 disables smoothing; 6 is a typical value" />
  <pref name="inputSimplify" type="int" min="0" max="100" group="Input"
      title="Simplification" description="Simplification threshold in 0.05 pixel steps" />
  <pref name="useWintab" type="bool" group="Input"
      title="Use Wintab if available" description="Use Wacom Wintab tablet interface" exclude="mobile,linux,mac" />

  <!-- should be limited to the "chrome", i.e, excluding any behaviors of ScribbleArea -->
  <!-- pref type="label" group="User Interface"
      title="Changes may not take effect until Write is restarted." / -->
  <pref name="uiTheme" type="int" group="User Interface"
      enum="Dark;Light" enumvals="1;2"
      title="Theme" description="User interface style" />
  <pref name="doubleTapSticky" type="bool" group="User Interface"
      title="Return to previous tool" description="Double tap to lock current tool" />
  <pref name="popupToolbar" type="bool" group="User Interface"
      title="Popup selection tools" description="Show floating toolbar when selection is made" />
  <!-- pref name="scrollerLocation" type="int" group="User Interface"
      enum="Right;Left" title="Scrollbar position" description="" / -->
  <pref name="autoHideScroller" type="bool" group="User Interface"
      title="Hide scroll handle" description="after 2 seconds" />
  <pref name="savePenMode" type="int" group="User Interface"
      enum="Manual;Recent pens;Recent per document"
      title="Saving pens" description="Method used to populate saved pen list" />
  <pref name="undoCircleSteps" type="int" min="1" max="360" step="1" group="User Interface"
      title="Undo dial steps" description="Number of undo/redo steps per revolution of undo dial" />
  <pref name="keepScreenOn" type="bool" group="User Interface" exclude="desktop"
      title="Keep screen on" description="" />
  <pref name="backKeyEnabled" type="bool" group="User Interface" exclude="desktop,ios"
      title="Back key enabled" description="" />
  <pref name="volButtonMode" type="int" group="User Interface" exclude="desktop,ios"
      enum="None (volume);Undo/Redo;Zoom out/in;Next page/Prev page;Prev page/Next page;Emulate pen button"
      title="Volume button action" description="" />
  <pref name="drawCursor" type="int" group="User Interface"
      enum="Hide;System;Custom" title="Cursor mode" description="Cursor drawing mode" />

  <pref name="thumbFirstPage" type="bool" level="1" group="Document List"
      title="Use first page for thumbnail" description="to serve as title page" />
  <pref name="savePrompt" type="bool" group="Document List" exclude="mobile"
      title="Prompt before saving document" description="when closing a document" />
  <pref name="reopenLastDoc" type="bool" level="1" group="Document List" exclude="mobile"
      title="Reopen last document" description="on application start" />
  <pref name="docListSiloed" type="bool" group="Document List" exclude="desktop,ios"
      title="Restrict to document folder" description="Prevent navigation out of document folder" />

  <pref name="growDown" type="bool" level="1" group="General"
      title="Auto grow page down" description="Allow page height to increase" />
  <pref name="growRight" type="bool" group="General"
      title="Auto grow page right" description="Allow page width to increase" />
  <pref name="growWithPen" type="bool" group="General"
      title="Grow page with strokes" description="Writing near edge of page will enlarge page" />
  <pref name="applyPenToSel" type="bool" group="General"
      title="Apply pen to selection" description="when pen changed with selection present" />
  <pref name="eraseOnImage" type="bool" group="General"
      title="Apply free eraser to images" description="Allow free erase tool to act on images" />
  <pref name="viewMode" type="int" group="General"
      enum="Single page;Vertical scrolling;Horizontal scrolling"
      title="View mode" description="" />

  <pref name="reflow" type="bool" group="Ruled Tools"
      title="Insert space reflow" description="Wrap strokes to next line when inserting space" />
  <pref name="insSpaceErase" type="bool" group="Ruled Tools"
      title="Erase when removing space" description="Erase strokes covered by ruled insert space" />
  <pref name="columnDetectMode" type="int" group="Ruled Tools"
      enum="None;Normal;Full page"
      title="Column detection" description="Detect columns created by tall strokes" />
  <pref name="minWordSep" type="float" min="0" max="4" step="0.1" group="Ruled Tools"
      title="Minimum reflow word break" description="As multiple of Y ruling" />
  <pref name="blankYRuling" type="float" level="1" min="0" max="400" step="1" group="Ruled Tools"
      title="Effective Y ruling for unruled pages"
      description="When ruled mode tools are used on unruled page" />

  <pref name="sRGB" type="bool" group="Advanced"
      title="Linear color interpolation (reopen)" description="sRGB-correct rendering" />
  <pref name="glRender" type="bool" group="Advanced"
      title="GPU rendering (reopen)" description="Use OpenGL for rendering" exclude="ios" />
  <pref name="savePicScaled" type="bool" group="Advanced"
      title="Reduce image size" description="Reduce image resolution to match size in document" />
  <pref name="docFileExt" type="string" group="Advanced" exclude="ios"
      enum="svgz;svg;html"
      title="File type" description="Default is svgz" />
  <!-- separate file type pref for iOS to exclude html -->
  <pref name="docFileExt" type="string" group="Advanced" exclude="desktop,android"
      enum="svgz;svg"
      title="File type" description="Default is svgz" />
  <pref name="autoSaveInterval" type="int" min="0" max="3600" step="30" group="Advanced"
      title="Auto save interval (seconds)" description="Set to 0 to disable" />
  <pref name="screenDPI" type="int" min="0" max="1000" step="10" group="Advanced"
      title="Force screen DPI (reopen)" description="Set to 0 to use detected value" />
  <pref name="syncServer" type="string" group="Advanced"
      title="Whiteboard server" description="For real-time collaboration" />
  <pref name="updateCheck" type="bool" level="1" group="Advanced" exclude="mobile"
      title="Check for updates (weekly)" description="Check online for updates once a week" />
  <pref name="showAdvPrefs" type="bool" level="1" group="Advanced" title="Show advanced preferences" description="" />
  <pref name="Reset Prefs" type="button" group="Advanced" />
  <pref name="About Write" type="button" level="1" group="Advanced" />

</map>
)#";

const char* clippingDocHTML =
R"#(<html xmlns="http://www.w3.org/1999/xhtml">
 <head>
  <title>clippings</title>
  <script type="text/writeconfig">
   <int name="drawCursor" value="1" />
   <int name="saveThumbnail" value="0" />
   <int name="saveUnmodified" value="1" />
   <float name="horzBorder" value="6" />
   <float name="BORDER_SIZE" value="0.1" />
  </script>
 </head>
 <body>

<svg width='67.4px' height='67.4px'>
<g class="write-content write-v3" xruling="0" yruling="0" marginLeft="0" papercolor="#FFFFFF">
<circle transform='translate(18.7,18.7)' fill='none' stroke='#000000' stroke-width='1.4' cx='15' cy='15' r='30'/>
</g>
</svg>

<svg width='67.4px' height='67.4px'>
<g class="write-content write-v3" xruling="0" yruling="0" marginLeft="0" papercolor="#FFFFFF">
<path class="write-stroke-pen" transform='translate(3.7,3.7)' fill='none' stroke='#000000' stroke-width='1.4'
    stroke-linecap='round' stroke-linejoin='round' d='M0 0 l0 60 60 0 0 -60 -60 0'/>
</g>
</svg>

<svg width='67.4px' height='67.4px'>
<g class="write-content write-v3" xruling="0" yruling="0" marginLeft="0" papercolor="#FFFFFF">
<path class="write-stroke-pen" transform='translate(3.7,3.7)' fill='none' stroke='#000000' stroke-width='1.4'
    stroke-linecap='round' stroke-linejoin='round' d='M30 0 l-30 60 60 0 -30 -60'/>
</g>
</svg>

<svg width='67.4px' height='67.4px'>
<g class="write-content write-v3" xruling="0" yruling="0" marginLeft="0" papercolor="#FFFFFF">
<path class="write-stroke-pen" transform='translate(3.7, 7.7)' fill='none' stroke='#000000' stroke-width='1.4'
    stroke-linecap='round' stroke-linejoin='round' d='M0 25.98L15 0L45 0L60 25.98L45 51.96L15 51.96L0 25.98'/>
</g>
</svg>

 </body>
</html>
)#";
