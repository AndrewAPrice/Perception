======================
LLVM 3.5 Release Notes
======================

.. contents::
    :local:

.. warning::
   These are in-progress notes for the upcoming LLVM 3.6 release.  You may
   prefer the `LLVM 3.5 Release Notes <http://llvm.org/releases/3.5.0/docs
   /ReleaseNotes.html>`_.


Introduction
============

This document contains the release notes for the LLVM Compiler Infrastructure,
release 3.6.  Here we describe the status of LLVM, including major improvements
from the previous release, improvements in various subprojects of LLVM, and
some of the current users of the code.  All LLVM releases may be downloaded
from the `LLVM releases web site <http://llvm.org/releases/>`_.

For more information about LLVM, including information about the latest
release, please check out the `main LLVM web site <http://llvm.org/>`_.  If you
have questions or comments, the `LLVM Developer's Mailing List
<http://lists.cs.uiuc.edu/mailman/listinfo/llvmdev>`_ is a good place to send
them.

Note that if you are reading this file from a Subversion checkout or the main
LLVM web page, this document applies to the *next* release, not the current
one.  To see the release notes for a specific release, please see the `releases
page <http://llvm.org/releases/>`_.

Non-comprehensive list of changes in this release
=================================================

.. NOTE
   For small 1-3 sentence descriptions, just add an entry at the end of
   this list. If your description won't fit comfortably in one bullet
   point (e.g. maybe you would like to give an example of the
   functionality, or simply have a lot to talk about), see the `NOTE` below
   for adding a new subsection.

* Support for AuroraUX has been removed.

* Added support for a `native object file-based bitcode wrapper format
  <BitCodeFormat.html#native-object-file>`_.

* ... next change ...

.. NOTE
   If you would like to document a larger change, then you can add a
   subsection about it right here. You can copy the following boilerplate
   and un-indent it (the indentation causes it to be inside this comment).

   Special New Feature
   -------------------

   Makes programs 10x faster by doing Special New Thing.

Prefix data rework
------------------

The semantics of the ``prefix`` attribute have been changed. Users
that want the previous ``prefix`` semantics should instead use
``prologue``.  To motivate this change, let's examine the primary
usecases that these attributes aim to serve,

  1. Code sanitization metadata (e.g. Clang's undefined behavior
     sanitizer)

  2. Function hot-patching: Enable the user to insert ``nop`` operations
     at the beginning of the function which can later be safely replaced
     with a call to some instrumentation facility.

  3. Language runtime metadata: Allow a compiler to insert data for
     use by the runtime during execution. GHC is one example of a
     compiler that needs this functionality for its
     tables-next-to-code functionality.

Previously ``prefix`` served cases (1) and (2) quite well by allowing the user
to introduce arbitrary data at the entrypoint but before the function
body. Case (3), however, was poorly handled by this approach as it
required that prefix data was valid executable code.

In this release the concept of prefix data has been redefined to be
data which occurs immediately before the function entrypoint (i.e. the
symbol address). Since prefix data now occurs before the function
entrypoint, there is no need for the data to be valid code.

The previous notion of prefix data now goes under the name "prologue
data" to emphasize its duality with the function epilogue.

The intention here is to handle cases (1) and (2) with prologue data and
case (3) with prefix data. See the language reference for further details
on the semantics of these attributes.

This refactoring arose out of discussions_ with Reid Kleckner in
response to a proposal to introduce the notion of symbol offsets to
enable handling of case (3).

.. _discussions: http://lists.cs.uiuc.edu/pipermail/llvmdev/2014-May/073235.html


Changes to the ARM Backend
--------------------------

 During this release ...


Changes to the MIPS Target
--------------------------

During this release ...

Changes to the PowerPC Target
-----------------------------

During this release ...

External Open Source Projects Using LLVM 3.6
============================================

An exciting aspect of LLVM is that it is used as an enabling technology for
a lot of other language and tools projects. This section lists some of the
projects that have already been updated to work with LLVM 3.6.

* A project


Additional Information
======================

A wide variety of additional information is available on the `LLVM web page
<http://llvm.org/>`_, in particular in the `documentation
<http://llvm.org/docs/>`_ section.  The web page also contains versions of the
API documentation which is up-to-date with the Subversion version of the source
code.  You can access versions of these documents specific to this release by
going into the ``llvm/docs/`` directory in the LLVM tree.

If you have any questions or comments about LLVM, please feel free to contact
us via the `mailing lists <http://llvm.org/docs/#maillist>`_.

