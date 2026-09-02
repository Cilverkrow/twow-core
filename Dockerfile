# Tortoise-WoW core (mangosd + realmd), Linux build, standalone.
#
# WHY THIS FILE IS HERE. Before it existed the answer to "what does the server
# need in order to build" was written down twice: once inline in a YAML step in
# .github/workflows/ci.yml, and once in twow-repo's deploy/docker/Dockerfile.core.
# Two copies of one apt list drift, and the copy that drifts is always the one
# nobody is looking at. The list belongs next to the code it describes, which is
# here. Wiring ci.yml to build FROM this file instead of pasting the list again
# is a deliberate follow-up, not something this file does on its own.
#
# STAGE LAYOUT
#
#   builder-base --> builder --> runtime
#
#   builder-base  the toolchain and the -dev packages. Nothing here depends on
#                 the source tree, so it is the stage a CI job can be run inside
#                 (docker build --target builder-base, then docker run in it) to
#                 get exactly the compiler, ACE and Boost this image ships.
#   builder       configure + build + install into ${PREFIX}.
#   runtime       debian-slim plus the shared libraries only, and ${PREFIX}
#                 copied out of the builder. The default target.
#
# Client data (dbc/maps/vmaps/mmaps) is deliberately NOT in this image. It comes
# out of a game client and is mounted at runtime.

# --------------------------------------------------------------------------
# THINGS THAT ARE LOAD-BEARING, IN DESCENDING ORDER OF WHAT THEY HAVE COST
# --------------------------------------------------------------------------
#
# 1. ca-certificates is not padding, and stays in the list whether or not
#    today's build appears to reach the network. It is absent from debian:trixie
#    and every https call made without it -- git over https, CMake's
#    FetchContent, curl -- fails with a certificate error that names TLS and
#    never names the missing package. Configuring with -DBUILD_TESTING=ON makes
#    CMake FetchContent-clone googletest, which is where this bites: git retries
#    three times with "Problem with the SSL CA cert (path? access rights?)" and
#    cmake gives up. Two separate debugging sessions, once in a container and
#    once in CI, went into rediscovering that.
#
# 2. CMAKE_INSTALL_PREFIX is the same value in the builder and in the runtime
#    stage, and has to be. The prefix is compiled into the binary as SYSCONFDIR
#    (see the DEFINITIONS block in CMakeLists.txt), so building under /build and
#    copying the tree somewhere else produces a server that silently cannot find
#    its own configuration -- no error, just a server missing every setting the
#    .conf would have given it. Build where you will run.
#
# 3. TW_ARCH defaults to x86-64-v2 rather than the historical `native`, so the
#    image runs on machines other than the one that built it. Left at the
#    CMakeLists default here; overriding it to `native` for a container image
#    produces an illegal instruction at startup on any other host, with no
#    warning at build time.
#
# 4. src/modules/Eluna IS A SUBMODULE and BUILD_ELUNA defaults ON, so the build
#    context has to come from a RECURSIVE checkout:
#
#        git clone --recurse-submodules <url> twow-core
#        # or, in an existing checkout:
#        git submodule update --init --recursive src/modules/Eluna
#
#    Eluna is left ON deliberately rather than switched off to dodge the
#    submodule: it is on for everybody else who builds this tree (CI checks out
#    with `submodules: recursive` for exactly this reason), and an image built
#    with -DBUILD_ELUNA=OFF would be the only artefact in the project without
#    the Lua engine -- a difference that shows up as missing scripts at runtime
#    rather than as a build failure. The guard in the builder stage below turns
#    a missing submodule into one line instead of a FATAL_ERROR a screen deep in
#    a configure log.
#
# 5. .git has to be in the build context. cmake/revision.h.cmake shells out to
#    git for the commit hash, and with no repository it falls back to
#    "unknown 1970-01-01 (Archived)" -- silently, because that fallback is a
#    deliberate feature for source tarballs. A shallow clone degrades it the
#    same way, which is why CI passes fetch-depth: 0. Two traps follow:
#      - a .dockerignore that excludes .git costs you the revision;
#      - a build context taken from a `git worktree` has a .git FILE pointing at
#        a path on the host, which does not exist inside the container, and git
#        reports "fatal: not a git repository". Build from a real clone.
#
# 6. No BuildKit cache mounts anywhere in this file. --mount=type=cache needs
#    buildx, and on a daemon without it the build dies at the first RUN that
#    uses one. Worse, `docker compose build` reports exit 0 for that failure, so
#    the breakage is invisible unless you use `docker build` directly. The cost
#    is a cold apt and a cold compile every time; the benefit is a file that
#    builds on a plain daemon.

ARG DEBIAN_VERSION=trixie
ARG PREFIX=/opt/turtle

# ------------------------------------------------------------- builder-base
#
# The compile toolchain, and nothing that depends on the source tree.
#
# Kept as its own stage so that a CI job can build its binaries inside exactly
# this image: same Debian, same gcc, same libACE and libmariadb sonames as the
# runtime stage below. Building the server on a runner's Ubuntu instead links
# against libACE-7.1.3 and libmysqlclient.so.21, neither of which exists in a
# trixie runtime -- binaries that install fine and then fail to exec.
#
# Deliberately NOT libboost-all-dev: it pulls in ~230 packages including
# OpenMPI, and only thread, filesystem, system and stacktrace are linked.
#
# python3 is not used by the build itself. It is here because the tooling that
# reads this build's compile_commands.json is Python, and it costs nothing that
# ships -- this whole stage is discarded and only ${PREFIX} is copied out.
FROM debian:${DEBIAN_VERSION}-slim AS builder-base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ccache python3 \
      ca-certificates \
      libace-dev default-libmysqlclient-dev \
      libssl-dev zlib1g-dev libbz2-dev \
      libboost-dev libboost-thread-dev libboost-filesystem-dev \
      libboost-system-dev libboost-stacktrace-dev \
    && rm -rf /var/lib/apt/lists/*

# ------------------------------------------------------------------ builder
FROM builder-base AS builder
ARG PREFIX
ARG BUILD_TYPE=Release

# DEBUG_SYMBOLS defaults to ON in CMakeLists.txt and is turned OFF here, which
# is the single biggest thing standing between this file and a machine that can
# actually run it. With symbols the object tree is around 20 GB and mangosd
# links to 750 MB; stripped, that same binary is 21 MB and realmd goes from
# 12 MB to 0.8 MB. Those are measured numbers from this tree, not estimates. An
# image is not where anyone attaches a debugger, so the symbols buy nothing here
# and cost an order of magnitude in disk. Pass --build-arg DEBUG_SYMBOLS=ON for
# a debuggable image, and have the room before you do.
ARG DEBUG_SYMBOLS=OFF

# The core's MODULES option defaults to "disabled", and that default is kept.
# This image therefore builds mangosd and realmd and compiles NONE of the core's
# own modules/ tree (mod-playerbots, mod-dungeon-clear, templates). Two reasons,
# and neither is expedience:
#
#   - it is the configuration CI asserts, namely that the core builds knowing
#     nothing about any platform that might later attach to it;
#   - twow-repo, the one consumer that does want modules compiled in, overrides
#     TW_MODULES_DIR to point at its OWN modules/ tree. A core image that baked
#     in the core's copy would be shipping the tree that gets overridden.
#
# To build the core's own modules anyway: --build-arg MODULES=static
ARG MODULES=disabled

WORKDIR /src
# The whole context, .git and the Eluna submodule included. There is no separate
# COPY for the submodule and there must not be: once the checkout is recursive
# it is an ordinary directory on disk, and a special-case COPY would only paper
# over a non-recursive one.
COPY . /src

# git refuses to read a tree whose owner does not match the calling uid. During
# `docker build` the context is copied in and owned by root, so this is usually
# a no-op -- but a refusal here does not fail the build, it makes
# cmake/revision.h.cmake take its 1970 fallback path silently. One line to close
# off a failure mode that is invisible until somebody tries to match a running
# server back to the commit that built it.
RUN git config --global --add safe.directory /src

# Fail on a non-recursive context here rather than inside cmake. The FATAL_ERROR
# in the root CMakeLists says the right thing, but it says it after a screen of
# configure output, and "Eluna submodule is missing" scrolling past in a docker
# build log reads like a warning.
RUN test -f /src/src/modules/Eluna/LuaEngine.h || { \
      echo "ERROR: src/modules/Eluna is empty -- this build context is not a recursive checkout."; \
      echo "       git submodule update --init --recursive src/modules/Eluna"; \
      exit 1; \
    }

# BUILD_ELUNA_TESTS defaults ON and pulls a Lua runtime smoke test into the
# `all` target. This build names its targets explicitly so the test would not be
# built regardless, but leaving the option on still calls enable_testing() and
# adds CTest machinery to an image that has no test runner in it.
#
# No -D on this line names a module, a module directory, or the module
# framework. That is the point: this command line is also an assertion about the
# core, the same one ci.yml makes.
RUN cmake -S /src -B /build -G Ninja \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -DCMAKE_INSTALL_PREFIX=${PREFIX} \
      -DDEBUG_SYMBOLS=${DEBUG_SYMBOLS} \
      -DMODULES=${MODULES} \
      -DBUILD_ELUNA_TESTS=OFF

# Two targets rather than `all`, and `cmake --install` afterwards rather than
# building the install target: mangosd and realmd are what this image is for,
# and every install rule that fires for them -- the two binaries, run-mangosd,
# and the generated mangosd.conf.dist and realmd.conf.dist -- is satisfied by
# having built exactly those two.
#
# /build is on the container filesystem and is discarded with this stage, so the
# object tree never reaches the final image. df is printed because the object
# tree is the largest thing this build touches by far and a "no space left on
# device" partway through a link reads like a compiler failure and is not one.
RUN cmake --build /build --target mangosd realmd --parallel "$(nproc)" \
    && cmake --install /build \
    && strip ${PREFIX}/bin/mangosd ${PREFIX}/bin/realmd \
    && df -h /

# ------------------------------------------------------------------ runtime
#
# Runtime shared libraries only. The soname-pinned package names are trixie's
# and are what the -dev packages in builder-base resolve to. Pinned rather than
# spelled as the -dev packages so that an unnoticed ABI bump in a later Debian
# fails this image build instead of failing at exec time.
#
# mariadb-client is not part of the server. It is here because every operational
# path around this image -- a migration, a schema check, a manual query against
# a running world -- otherwise needs a second container to talk to the database.
FROM debian:${DEBIAN_VERSION}-slim AS runtime
ARG PREFIX
ENV PREFIX=${PREFIX}
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      libace-8.0.2 libmariadb3 libssl3 zlib1g libbz2-1.0 \
      libboost-system1.83.0 libboost-thread1.83.0 libboost-filesystem1.83.0 \
      mariadb-client \
    && rm -rf /var/lib/apt/lists/*

# An unprivileged uid, pinned rather than allocated. `useradd --system` with no
# --uid takes the highest free system uid at image build time -- 999 today, 998
# the day an apt package claims an account ahead of it -- and an identity that
# moves between builds cannot be granted anything in advance on the host. The
# configs this server reads carry a database password in cleartext and are
# therefore not world-readable, so the host has to chown or setfacl them to a
# number it knows before the container starts.
#
# 10001 rather than something low: above the 0-999 range Debian hands to system
# accounts and above the 1000-1999 band that desktop logins and CI runners sit
# in, so it collides with nothing already present on a host whose files this
# container is about to be handed.
#
# Changing this number on an existing deployment orphans any named volume the
# old image created: Docker takes a volume's ownership from the image directory
# the first time it is mounted, so a volume made under the previous uid is not
# writable by the new one.
RUN groupadd --system --gid 10001 turtle \
    && useradd --system --uid 10001 --gid 10001 \
         --create-home --home-dir /home/turtle turtle \
    && mkdir -p /data /var/log/turtle ${PREFIX}/run \
    && chown -R turtle:turtle /data /var/log/turtle ${PREFIX}/run

COPY --from=builder ${PREFIX} ${PREFIX}
ENV PATH="${PREFIX}/bin:${PATH}"

WORKDIR ${PREFIX}/bin
# 8090 world, 3724 auth.
EXPOSE 8090 3724
USER turtle

# No ENTRYPOINT, and that is a decision rather than an omission. mangosd reads
# its console from stdin and EXITS ON EOF, so `docker run <image>` with no -i
# starts the world server and then watches it stop. Keeping a real deployment
# alive means holding a writable FIFO open on the daemon's stdin, and the
# entrypoint that does that also renders configs and waits for the database --
# all of which is deployment policy belonging to whatever orchestrates this
# image, not to the core. twow-repo supplies its own. This image is the binaries
# and their libraries, and nothing about how to run them.
#
# `docker run --rm <image> mangosd --version` works as-is and is the cheapest
# check that the image is sound.
CMD ["mangosd", "--version"]
