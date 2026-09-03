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
                     (std-files
                      (find-files toolchain "^std\\.cc$"))
                     (std
                      (find (lambda (file)
                              (string-contains file "/bits/std.cc"))
                            std-files)))
                (unless std
                  (error "libstdc++ bits/std.cc is not in the GCC toolchain"))
                (let* ((std-directory (dirname (dirname std)))
                       (config-files
                        (find-files toolchain "^c\\+\\+config\\.h$"))
                       (config
                        (find (lambda (file)
                                (string-contains file "/bits/c++config.h"))
                              config-files))
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
                  (format #t "std module source: ~a~%" std)
                  (format #t "standard library include flags: ~a~%"
                          include-flags)
                  (call-with-output-file "/tmp/libstdc++.modules.json"
                    (lambda (out)
                      (format out
                              "{~%  \"version\": 1,~%  \"revision\": 1,~%  \"modules\": [~%    { \"logical-name\": \"std\", \"source-path\": \"~a\",~%      \"is-std-library\": true },~%    { \"logical-name\": \"std.compat\", \"source-path\": \"~a/bits/std.compat.cc\",~%      \"is-std-library\": true }~%  ]~%}~%"
                              std std-directory)))))))
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
     (map (lambda (name) (list name (needed name)))
          (list "mesa" "libglvnd" "libxkbcommon" "wayland"
                "wayland-protocols" "libx11" "libxrandr" "libxinerama"
                "libxcursor" "libxi" "alsa-lib" "pulseaudio" "dbus"
                "elogind"
                "boost" "skia" "libzip" "libsndfile" "mpg123" "openal"
                "glfw" "xz" "zlib" "libpng" "libjpeg-turbo" "freetype"
                "expat" "flac" "fmt" "libogg" "opus" "libvorbis"
                "vulkan-headers"
                ;; Asio's TLS has one backend and this is it.
                "openssl")))
    (home-page "https://github.com/j4niwzis/osu-cpp")
    (synopsis "Native client for osu! beatmaps")
    (description
     "A client that reads osu! beatmaps and plays them, drawing through Skia
and reaching the machine through GLFW, OpenAL and libsndfile.")
    (license license:agpl3)))

osu-cpp
