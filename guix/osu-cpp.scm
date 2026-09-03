;;; Building this client with Guix.
;;;
;;; A Guix build has no network and no working directory it can be handed
;;; things in: every input is a store item decided before the build starts.
;;; Libraries are Guix packages. Only the two application components that
;;; no distribution packages, skiff and skiff-widgets, come from sources.scm.
;;; CME_SYSTEM=ALWAYS makes an absent Guix library an error rather than
;;; silently building another copy.
;;;
;;; sources.scm is generated from cme-lock.json by tools/lock-to-guix.py,
;;; which comes with cmake-everywhere. It holds a revision or an archive
;;; digest per library and nothing else.
;;;
;;;   guix build -f guix/osu-cpp.scm

(use-modules (guix packages)
             (guix gexp)
             (guix git-download)
             (guix download)
             (guix base16)
             (guix base32)
             (guix utils)
             (guix build-system cmake)
             (guix build-system gnu)
             ((guix licenses) #:prefix license:)
             (gnu packages)
             (ice-9 regex)
             (ice-9 textual-ports)
             (srfi srfi-1))

;; Packages by the name the command line uses, rather than by the module
;; each happens to be defined in: a wrong module is a file that will not
;; load, and the names are what `guix show` answers to.
(define (needed name)
  (specification->package name))

;; CMake as this project asks for it.
;;
;; standalone/CMakeLists.txt says 4.3.4 and means it: what it needs from
;; CMake is what CMake learned to do about C++ modules in imported targets.
;; Guix has 4.1.3 today and the release this runs from has 3.31.10, so the
;; version that is asked for is built from its own release with the recipe
;; Guix already has for CMake.
(define cmake-for-this
  (let ((base (needed "cmake-minimal")))
    (package
      (inherit base)
      (name "cmake-minimal")
      (version "4.3.4")
      (source
       (origin
         (method url-fetch)
         (uri (string-append
               "https://github.com/Kitware/CMake/releases/download/v"
               version "/cmake-" version ".tar.gz"))
         (sha256
          (base32 "1nmd67lrk14pq6bqlrr5yc6l9qdpdvf1wawzadjdfjgbp6bzivzx"))))
      (arguments
       (substitute-keyword-arguments (package-arguments base)
         ;; This is a bootstrapping build tool, not the program under test.
         ;; The inherited suite has hundreds of upstream CMake tests and its
         ;; time-sensitive CTestTestStopTime test is flaky on shared runners.
         ;; Testing osu-cpp below must not depend on that runner's clock load.
         ((#:tests? _ #t) #f)
         ;; Guix's recipe deletes the help documentation by a path it spells
         ;; out with the version of another package -- cmake-bootstrap, which
         ;; is 3.31.10 -- so with the sources changed the phase looked for
         ;; share/cmake-3.31/Help in a tree that has share/cmake-4.3, and
         ;; stopped after a CMake that had just built and installed.
         ;;
         ;; Kept rather than deleted at another path: the documentation is
         ;; not in the way, and a phase whose only job is to save space is
         ;; not worth teaching a second version number.
         ((#:phases phases)
          #~(modify-phases #$phases
              (delete 'delete-help-documentation))))))))

(define %here (dirname (current-filename)))
(define %top (dirname %here))

;; Which cmake-everywhere, read from the file that pins it rather than
;; repeated here: two places that say a revision are one place that is wrong.
(define %pinned
  (call-with-input-file (string-append %top "/cmake/get_cme.cmake")
    get-string-all))

(define (pinned-field pattern)
  (let ((found (string-match pattern %pinned)))
    (if found
        (match:substring found 1)
        (error "guix/osu-cpp.scm: cmake/get_cme.cmake does not say" pattern))))

(define %cme-revision (pinned-field "CME_PINNED \"([0-9a-f]{40})\""))
(define %cme-digest (pinned-field "CME_PINNED_SHA256 \"([0-9a-f]{64})\""))

(define %cme
  (origin
    (method url-fetch)
    (uri (string-append "https://github.com/j4niwzis/cmake-everywhere/archive/"
                        %cme-revision ".tar.gz"))
    (file-name (string-append "cmake-everywhere-"
                              (string-take %cme-revision 8) ".tar.gz"))
    (sha256 (base16-string->bytevector %cme-digest))))

;; (port version origin) for every library the lock names.
(define %all-sources
  (if (file-exists? (string-append %here "/sources.scm"))
      (load (string-append %here "/sources.scm"))
      (error "guix/osu-cpp.scm: guix/sources.scm is not there. A build with \
a network writes it: tools/lock-to-guix.py standalone/cme-lock.json \
$CPM_SOURCE_CACHE > guix/sources.scm")))

(define %sources
  (filter (lambda (entry)
            (member (first entry) '("skiff" "skiff-widgets")))
          %all-sources))

;; Skia, at the milestone this program is written against.
;;
;; The distribution packages one, and it is older than the type this client
;; needs. The probe says so exactly:
;;
;;   error: no type named 'Uniform' in 'SkRuntimeEffect'
;;   error: no member named 'findUniform' in 'SkRuntimeEffect'
;;
;; -- and the slider bodies are drawn by a runtime effect whose uniforms are
;; set by name through SkRuntimeEffectBuilder. So this is the package that
;; distribution is missing, defined here: the milestone every other build of
;; this program uses, from the archive sources.scm already pins, built the
;; way the port builds it -- GN, everything it can take from the system
;; taken from the system, and nothing from third_party/externals, which no
;; build here ever fetches.
;;
;; The layout is the one an installed Skia has: its public headers under
;; include/skia, which is what makes "include/core/SkCanvas.h" -- the way
;; Skia's headers include each other -- resolve for whoever links it.
(define skia-for-this
  (let* ((entry (or (find (lambda (item) (string=? (first item) "skia"))
                          %all-sources)
                    (error "guix/osu-cpp.scm: sources.scm names no skia")))
         (skia-version (second entry))
         ;; Named for what it is.
         ;;
         ;; sources.scm calls this one "skia-153", and how a Guix build
         ;; unpacks a source is decided by that name: with no extension on
         ;; it the unpack phase copied the archive into the build directory
         ;; and called it done -- "phase `unpack' succeeded after 0.0
         ;; seconds", a directory with a tarball in it and no tree. The
         ;; digest is of the contents and does not change with the name.
         (skia-source (origin
                        (inherit (third entry))
                        (file-name (string-append "skia-" (second entry)
                                                  ".tar.gz")))))
    (package
      (name "skia")
      (version skia-version)
      (source skia-source)
      (build-system gnu-build-system)
      (arguments
       (list
        #:tests? #f
        #:modules '((guix build gnu-build-system)
                    (guix build utils)
                    (srfi srfi-1)
                    (ice-9 ftw))
        #:phases
        #~(modify-phases %standard-phases
            (delete 'bootstrap)
            (delete 'patch-generated-file-shebangs)
            ;; Where the tree actually is.
            ;;
            ;; GN finds what to build by looking for a .gn file in the
            ;; directory it is run in or above it, and says so plainly when
            ;; it cannot: "Can't find source root". Which directory that is
            ;; depends on how the archive unpacked, so it is looked for
            ;; rather than assumed, and said out loud either way.
            (add-after 'unpack 'enter-the-source
              (lambda _
                (unless (file-exists? ".gn")
                  (let ((inside (scandir "."
                                         (lambda (name)
                                           (and (not (member name '("." "..")))
                                                (file-is-directory? name))))))
                    (when (= (length inside) 1)
                      (chdir (first inside)))))
                (format #t "source root: ~a~%" (getcwd))
                (format #t "and it has a .gn: ~a~%" (file-exists? ".gn"))
                (unless (file-exists? ".gn")
                  (error "no .gn in the Skia archive; GN has no build to read"))))
            (replace 'configure
              (lambda* (#:key inputs #:allow-other-keys)
                ;; What the port says, in the spelling GN reads. Nothing is
                ;; compiled out of third_party/externals: this tree has
                ;; none, so a bundled path would not quietly happen, it
                ;; would fail to find its sources.
                (invoke
                 "gn" "gen" "out"
                 (string-append
                  "--args="
                  (string-join
                   (list "is_official_build=true"
                         "is_component_build=false"
                         ;; Skia asks its BUILDCONFIG which compiler this is
                         ;; by running "cc --version", and there is no cc in
                         ;; a Guix build environment -- the compiler is gcc.
                         ;; The same GCC 15 that supplies the standard
                         ;; library this program is compiled against, so
                         ;; that what links Skia and what compiles the
                         ;; client are one libstdc++.
                         "cc=\"gcc\""
                         "cxx=\"g++\""
                         "skia_enable_tools=false"
                         "skia_enable_ganesh=true"
                         "skia_use_gl=true"
                         "skia_use_egl=false"
                         "skia_use_x11=false"
                         "skia_use_vulkan=false"
                         "skia_use_dawn=false"
                         "skia_use_freetype=true"
                         "skia_use_system_freetype2=true"
                         ;; The client carries its own fonts and asks Skia
                         ;; for no font database, so this is a dependency it
                         ;; would take and never use.
                         "skia_use_fontconfig=false"
                         "skia_use_libpng_decode=true"
                         "skia_use_libpng_encode=true"
                         "skia_use_system_libpng=true"
                         "skia_use_libjpeg_turbo_decode=true"
                         "skia_use_libjpeg_turbo_encode=true"
                         "skia_use_system_libjpeg_turbo=true"
                         "skia_use_libwebp_decode=false"
                         "skia_use_libwebp_encode=false"
                         "skia_use_zlib=true"
                         "skia_use_system_zlib=true"
                         "skia_use_expat=true"
                         "skia_use_system_expat=true"
                         "skia_use_harfbuzz=false"
                         "skia_use_icu=false"
                         "skia_enable_skunicode=false"
                         "skia_use_wuffs=false"
                         "skia_use_libavif=false"
                         "skia_use_libjxl_decode=false"
                         "skia_use_dng_sdk=false"
                         "skia_use_piex=false"
                         "skia_use_lua=false"
                         "skia_use_libheif=false"
                         "skia_enable_pdf=false"
                         "skia_enable_svg=false"
                         "skia_enable_skottie=false"
                         ;; Where FreeType's headers are.
                         ;;
                         ;; Skia's target for a system FreeType names
                         ;; /usr/include/freetype2 outright, and there is no
                         ;; /usr here -- the build stopped on "ft2build.h:
                         ;; No such file or directory" after six minutes of
                         ;; compiling. Every other system library it takes
                         ;; keeps its headers where the environment already
                         ;; points; this one keeps them a directory down.
                         (string-append
                          "extra_cflags=[\"-I"
                          (assoc-ref inputs "freetype")
                          "/include/freetype2\"]"))
                   " ")))))
            (replace 'build
              (lambda* (#:key parallel-build? #:allow-other-keys)
                (invoke "ninja" "-C" "out"
                        "-j" (number->string (if parallel-build?
                                                 (parallel-job-count)
                                                 1))
                        "skia")))
            (replace 'install
              (lambda _
                (let* ((out #$output)
                       (headers (string-append out "/include/skia"))
                       (lib (string-append out "/lib")))
                  (mkdir-p headers)
                  (copy-recursively "include"
                                    (string-append headers "/include"))
                  ;; The same headers under the name a consumer uses.
                  ;;
                  ;; Skia's own headers include each other from the root of
                  ;; its tree -- "include/core/SkTypes.h" -- and a program
                  ;; that links it includes them as <skia/core/SkCanvas.h>.
                  ;; Both are true of one directory once it has a skia in it
                  ;; that is its include, so one -I answers both and there
                  ;; is nothing for a consumer to arrange.
                  (symlink "include" (string-append headers "/skia"))
                  ;; modules/skcms is included as "modules/skcms/..." by the
                  ;; public headers, and it is public for that reason.
                  (when (file-exists? "modules")
                    (copy-recursively
                     "modules" (string-append headers "/modules")
                     #:select? (lambda (file stat)
                                 (or (eq? (stat:type stat) 'directory)
                                     (string-suffix? ".h" file)))))
                  (mkdir-p lib)
                  (install-file "out/libskia.a" lib)
                  (mkdir-p (string-append lib "/pkgconfig"))
                  (call-with-output-file
                      (string-append lib "/pkgconfig/skia.pc")
                    (lambda (port)
                      (format port
                              "prefix=~a~%libdir=${prefix}/lib~%\
includedir=${prefix}/include~%~%\
Name: skia~%Description: 2D graphics library~%Version: ~a~%\
Requires: freetype2 libpng libjpeg zlib expat~%\
Cflags: -I${includedir}/skia~%Libs: -L${libdir} -lskia~%"
                              out #$version)))))))))
      (native-inputs (list (needed "gn") (needed "ninja") (needed "python")
                           (needed "pkg-config")
                           (needed "gcc-toolchain@15")))
      (inputs (list (needed "expat") (needed "freetype")
                    (needed "libjpeg-turbo") (needed "libpng")
                    (needed "zlib") (needed "mesa") (needed "libglvnd")))
      (home-page "https://skia.org")
      (synopsis "2D graphics library")
      (description
       "Skia is the 2D graphics library this client draws through. This is
the milestone it is written against, which is newer than the one the
distribution packages.")
      (license license:bsd-3))))

;; Where the port declarations and the unpacked archives are written. A
;; store item is read-only and there is nowhere else with a name that both
;; the phase and the configure flags can say, so both say this one.
(define %ports "/tmp/cme-ports")

(define-public osu-cpp
  (package
    (name "osu-cpp")
    (version "1.0.0")
    (source (local-file %top "osu-cpp-checkout"
                        #:recursive? #t
                        #:select? (or (git-predicate %top) (const #t))))
    (build-system cmake-build-system)
    (arguments
     (list
      #:tests? #f
      ;; The CMake this needs, said where the build system reads it. A
      ;; cmake in the inputs is a cmake on PATH; the one that configures is
      ;; the build system's own, and it was still 3.31.10 -- "CMake 4.3.4 or
      ;; higher is required. You are running version 3.31.10", with 4.3.4
      ;; built and sitting in the profile.
      #:cmake cmake-for-this
      ;; Ninja, because C++ modules are compiled in an order only a build
      ;; system that reads a scanner's answers can produce, and Make is not
      ;; one. CMake refuses the combination outright.
      #:generator "Ninja"
      #:modules '((guix build cmake-build-system)
                  (guix build utils)
                  (ice-9 popen)
                  (ice-9 textual-ports)
                  (srfi srfi-1)
                  (srfi srfi-13))
      #:configure-flags
      #~(list "-DCMAKE_BUILD_TYPE=Release"
              "-DCMAKE_CXX_STDLIB_MODULES_JSON=/tmp/libstdc++.modules.json"
              "-DCME_OFFLINE=ON"
              "-DCME_SYSTEM=ALWAYS"
              "-DCME_SYSTEM_OSUCPP=OFF"
              "-DCME_SYSTEM_SKIFF=OFF"
              "-DCME_SYSTEM_SKIFF-WIDGETS=OFF"
              (string-append "-DCME_ARCHIVE=" #$%cme)
              (string-append "-DCME_OVERLAYS=" #$%ports))
      #:phases
      #~(modify-phases %standard-phases
          ;; The build is the one under standalone/. Out of source either
          ;; way, so the build directory is beside it.
          (add-after 'unpack 'enter-standalone
            (lambda _
              ;; A home that can be written to: the build runs with none,
              ;; and the source cache is the first thing to want one.
              (setenv "HOME" (getcwd))
              (chdir "standalone")))
          ;; libstdc++ carries the sources of its standard-library modules,
          ;; but no manifest that tells CMake where they are. Find the copy
          ;; this build's compiler actually searches and describe it, just
          ;; as the native and Nix builds do.
          (add-before 'configure 'write-stdlib-module-manifest
            (lambda* (#:key inputs #:allow-other-keys)
              ;; Guix's Clang wrapper injects its libstdc++ include paths
              ;; outside Clang's built-in search list. Use the explicit GCC
              ;; toolchain input instead of trying to rediscover wrapper
              ;; state from `clang++ -v`.
              (let* ((toolchain (assoc-ref inputs "gcc-toolchain"))
                     ;; Following symbolic links, because a Guix toolchain
                     ;; is a tree of them: include/c++ is a directory made
                     ;; of several inputs, and the target-specific part of
                     ;; it -- where bits/c++config.h is -- is a link to the
                     ;; compiler's own output. Not following them is why
                     ;; that header was reported as absent while the
                     ;; compiler was reading it every day.
                     (std-files
                      (find-files toolchain "^std\\.cc$" #:stat stat))
                     (std
                      (find (lambda (file)
                              (string-contains file "/bits/std.cc"))
                            std-files)))
                (unless std
                  (error "libstdc++ bits/std.cc is not in the GCC toolchain"))
                (let* ((std-directory (dirname (dirname std)))
                       (config-files
                        (find-files toolchain "^c\\+\\+config\\.h$"
                                    #:stat stat))
                       (config
                        (find (lambda (file)
                                (string-contains file "/bits/c++config.h"))
                              config-files))
                       ;; Without it the headers above have no configuration
                       ;; to read, and what answers <bits/c++config.h> is
                       ;; then whichever other standard library is on the
                       ;; include path. That is the failure this phase
                       ;; exists to prevent, so it is an error here.
                       (_ (unless config
                            (error "no bits/c++config.h in the GCC toolchain")))
                       (directories
                        (delete-duplicates
                         (filter identity
                          (list std-directory
                                (string-append std-directory "/backward")
                                (and config (dirname (dirname config)))))))
                       (include-flags
                        (string-join
                         (append-map (lambda (directory)
                                       (list "-isystem" directory))
                                     directories)
                         " ")))
                  ;; clang-scan-deps does not run Guix's compiler wrapper;
                  ;; put the same standard-library paths on CMake's command
                  ;; line so module dependency scanning sees them too.
                  (setenv "CXXFLAGS" include-flags)
                  ;; And the same standard library, only.
                  ;;
                  ;; Clang comes with libstdc++ include paths of its own --
                  ;; another GCC's -- and Guix hands them to it through
                  ;; CPLUS_INCLUDE_PATH, which no -isystem on the command
                  ;; line removes. What that produced was one compile with
                  ;; two standard libraries in it: <type_traits> from the
                  ;; toolchain named above and <bits/c++config.h> from
                  ;; Clang's, which is a GCC old enough not to define the
                  ;; macro that header uses --
                  ;;
                  ;;   type_traits:897: error: expected unqualified-id
                  ;;
                  ;; -- an unexpanded _GLIBCXX26_DEPRECATED_SUGGEST, and
                  ;; every probe of an installed library failing because of
                  ;; it. So the C++ entries of that variable are replaced by
                  ;; this toolchain's; everything else in it -- the C
                  ;; library, the inputs' headers -- stays.
                  (let* ((existing (or (getenv "CPLUS_INCLUDE_PATH") ""))
                         (others (filter (lambda (directory)
                                           (not (string-contains
                                                 directory "/include/c++")))
                                         (string-split existing #\:)))
                         (wanted (string-join
                                  (append directories others) ":")))
                    (setenv "CPLUS_INCLUDE_PATH" wanted)
                    (format #t "C++ include path: ~a~%" wanted))
                  (format #t "std module source: ~a~%" std)
                  (format #t "standard library include flags: ~a~%"
                          include-flags)
                  (call-with-output-file "/tmp/libstdc++.modules.json"
                    (lambda (out)
                      (format out
                              "{~%  \"version\": 1,~%  \"revision\": 1,~%  \"modules\": [~%    { \"logical-name\": \"std\", \"source-path\": \"~a\",~%      \"is-std-library\": true },~%    { \"logical-name\": \"std.compat\", \"source-path\": \"~a/bits/std.compat.cc\",~%      \"is-std-library\": true }~%  ]~%}~%"
                              std std-directory)))))))
          ;; The link CMake leaves in the source tree, taken back out.
          ;;
          ;; With CMAKE_EXPORT_COMPILE_COMMANDS on, CMake puts a
          ;; compile_commands.json in the build directory and a symbolic
          ;; link to it beside the sources. The phase that installs licence
          ;; files walks the source tree, and a link whose target is not
          ;; there stops that walk:
          ;;
          ;;   error: in phase 'install-license-files': system-error "stat"
          ;;   ("No such file or directory" "../compile_commands.json")
          ;;
          ;; -- after the program had built and installed.
          (add-before 'install-license-files 'forget-the-compile-commands
            (lambda _
              (let ((link "../compile_commands.json"))
                (when (false-if-exception (lstat link))
                  (delete-file link)))))
          ;; One declaration per application component, saying where its
          ;; pinned checkout is. Distribution libraries are Guix inputs.
          ;;
          ;; An input that is a checkout is already a tree; an input that is
          ;; an archive is one file, and a port needs a tree -- so it is
          ;; unpacked here rather than fetched unpacked, because the digest
          ;; the lock holds is the archive's and not the contents'.
          (add-before 'configure 'write-ports
            (lambda* (#:key inputs #:allow-other-keys)
              (for-each
               (lambda (input)
                 (let ((name (car input))
                       (item (cdr input)))
                   (when (string-prefix? "cme-source-" name)
                     (let* ((port (string-drop name (string-length "cme-source-")))
                            (place (string-append #$%ports "/" port))
                            (tree (string-append place "/source")))
                       (mkdir-p tree)
                       ;; Copied rather than pointed at. A store item is
                       ;; read-only and some projects write into their own
                       ;; source tree while configuring -- zlib renames
                       ;; zconf.h out of the way -- so a build from the
                       ;; store stops on a permission it was never given.
                       ;;
                       ;; An archive is unpacked as it is copied, because
                       ;; the digest the lock holds is the archive's. A
                       ;; release archive is its contents under one
                       ;; directory named after itself, which is not part of
                       ;; the tree the port expects.
                       (if (file-is-directory? item)
                           (copy-recursively item tree)
                           (invoke "tar" "xf" item "-C" tree
                                   "--strip-components=1"))
                       (invoke "chmod" "-R" "u+w" tree)
                       (call-with-output-file (string-append place "/port.cmake")
                         (lambda (out)
                           (format out
                                   "cme_declare_port(~%  NAME ~a~%  SOURCE_DIR \"~a\")~%"
                                   port tree)))))))
               inputs))))))
    ;; Labelled inputs throughout, because the sources have to be labelled:
    ;; the phase below finds them by a name it can read the port out of, and
    ;; a list that is half labelled and half not is not two styles, it is one
    ;; invalid input.
    (native-inputs
     (append
      (map (lambda (name) (list name (needed name)))
           (list "ninja" "pkg-config" "python" "gn" "meson"
                 "gperf" "clang-toolchain" "tar" "gzip" "xz" "bzip2"
                 "coreutils"
                 ;; For wayland-scanner, which glfw runs to generate its
                 ;; protocol bindings.
                 "wayland"))
      ;; Clang compiles the project; this explicit GCC 15 toolchain supplies
      ;; the libstdc++ C++23 module sources and headers it compiles against.
      (list (list "gcc-toolchain" (needed "gcc-toolchain@15")))
      ;; The application components the phase reads the port out of.
      (map (lambda (entry)
             (list (string-append "cme-source-" (first entry)) (third entry)))
           %sources)))
    (inputs
     (append
      (list (list "skia" skia-for-this))
      (map (lambda (name) (list name (needed name)))
          (list "mesa" "libglvnd" "libxkbcommon" "wayland"
                "wayland-protocols" "libx11" "libxrandr" "libxinerama"
                "libxcursor" "libxi" "alsa-lib" "pulseaudio" "dbus"
                "elogind"
                "boost" "libzip" "libsndfile" "mpg123" "openal"
                "glfw" "xz" "zlib" "libpng" "libjpeg-turbo" "freetype"
                "expat" "flac" "fmt" "libogg" "opus" "libvorbis"
                "vulkan-headers"
                ;; Asio's TLS has one backend and this is it.
                "openssl"))))
    (home-page "https://github.com/j4niwzis/osu-cpp")
    (synopsis "Native client for osu! beatmaps")
    (description
     "A client that reads osu! beatmaps and plays them, drawing through Skia
and reaching the machine through GLFW, OpenAL and libsndfile.")
    (license license:agpl3)))

osu-cpp
