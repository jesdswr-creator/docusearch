<?xml version="1.0" encoding="UTF-8"?>
<!--
  heat-filter.xsl
  ===============
  Applied to the output of `wix heat dir` to exclude files that are
  already declared in DocuSearch.wxs (would otherwise cause duplicate-
  symbol errors at light time).

  Excludes:
    * DocuSearch.exe   — declared as <File Id="DocuSearchExe"> in main wxs
    * tst_*.exe        — CTest unit-test binaries, never shipped

  WiX v4 heat emits Component elements whose child File element has a
  Source attribute like "$(var.BuildOutputDir)\DocuSearch.exe". We match
  on the trailing filename and drop the whole Component.
-->
<xsl:stylesheet version="1.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:wix="http://wixtoolset.org/schemas/v4/wxs">

  <xsl:output method="xml" indent="yes" />

  <!-- Identity transform: copy everything by default -->
  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()" />
    </xsl:copy>
  </xsl:template>

  <!-- Drop the DocuSearch.exe component (already in main wxs).
       Use contains() so it matches regardless of how heat emits the
       path prefix (with or without trailing slash, with var or absolute). -->
  <xsl:template match="wix:Component[
    contains(wix:File/@Source, '\DocuSearch.exe') or
    contains(wix:File/@Source, '/DocuSearch.exe')
  ]" />

  <!-- Drop any tst_*.exe test binaries (CTest artifacts that should
       never be shipped in the installer). -->
  <xsl:template match="wix:Component[
    contains(wix:File/@Source, '\tst_') or
    contains(wix:File/@Source, '/tst_')
  ]" />

</xsl:stylesheet>
