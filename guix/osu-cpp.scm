;;; Building this client with Guix.
;;;
;;; A Guix build has no network and no working directory it can be handed
;;; things in: every input is a store item decided before the build starts.
;;; So the shape is the Flatpak one and the Nix one again -- sources.scm says
;;; what to fetch, and a generated port declaration says where each one
;;; landed, which is the only thing cmake-everywhere needs to be told. What
;;; each library is and how it is built stays in the registry.
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
(define %sources
  (if (file-exists? (string-append %here "/sources.scm"))
      (load (string-append %here "/sources.scm"))
      (error "guix/osu-cpp.scm: guix/sources.scm is not there. A build with \
a network writes it: tools/lock-to-guix.py standalone/cme-lock.json \
$CPM_SOURCE_CACHE > guix/sources.scm")))

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
      #:modules '((guix build cmake-build-system)
                  (guix build utils)
                  (srfi srfi-1)
                  (srfi srfi-13))
      #:configure-flags
      #~(list "-DCMAKE_BUILD_TYPE=Release"
              "-DCME_OFFLINE=ON"
              (string-append "-DCME_ARCHIVE=" #$%cme)
              (string-append "-DCME_OVERLAYS=" #$%ports))
      #:phases
      #~(modify-phases %standard-phases
          ;; The build is the one under standalone/. Out of source either
          ;; way, so the build directory is beside it.
          (add-after 'unpack 'enter-standalone
            (lambda _
              (chdir "standalone")))
          ;; One declaration per library, saying where its sources are.
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
                            (tree (if (file-is-directory? item)
                                      item
                                      (let ((into (string-append place "/source")))
                                        (mkdir-p into)
                                        ;; A release archive is its contents
                                        ;; under one directory named after
                                        ;; itself, which is not part of the
                                        ;; tree the port expects.
                                        (invoke "tar" "xf" item "-C" into
                                                "--strip-components=1")
                                        into))))
                       (mkdir-p place)
                       (call-with-output-file (string-append place "/port.cmake")
                         (lambda (out)
                           (format out
                                   "cme_declare_port(~%  NAME ~a~%  SOURCE_DIR \"~a\")~%"
                                   port tree)))))))
               inputs))))))
    (native-inputs
     (append
      (map needed (list "cmake" "ninja" "pkg-config" "python" "gn" "meson"
                        "gperf" "clang-toolchain" "tar" "gzip" "xz" "bzip2"))
      ;; Every library the lock names, under a name the phase reads the port
      ;; out of.
      (map (lambda (entry)
             (list (string-append "cme-source-" (first entry)) (third entry)))
           %sources)))
    (inputs
     (map needed (list "mesa" "libglvnd" "libxkbcommon" "wayland"
                       "wayland-protocols" "libx11" "libxrandr" "libxinerama"
                       "libxcursor" "libxi" "alsa-lib" "pulseaudio" "dbus"
                       "elogind")))
    (home-page "https://github.com/j4niwzis/osu-cpp")
    (synopsis "Native client for osu! beatmaps")
    (description
     "A client that reads osu! beatmaps and plays them, drawing through Skia
and reaching the machine through GLFW, OpenAL and libsndfile.")
    (license license:agpl3)))

osu-cpp
