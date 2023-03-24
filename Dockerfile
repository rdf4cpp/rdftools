FROM alpine:3.17 AS builder
ARG MARCH="x86-64-v3"

RUN apk update && \
    apk add \
    make cmake autoconf automake pkgconfig \
    gcc g++ gdb \
    clang15 clang15-dev clang15-libs clang15-extra-tools clang15-static lldb llvm15 llvm15-dev\
    openjdk11-jdk \
    pythonispython3 py3-pip \
    bash git libtool util-linux-dev linux-headers \
    && \
    apk add mold --repository=https://mirrors.edge.kernel.org/alpine/edge/testing

ARG CC="clang"
ARG CXX="clang++"
ENV CXXFLAGS="${CXXFLAGS} -march=${MARCH}"
RUN rm /usr/bin/ld && ln -s /usr/bin/mold /usr/bin/ld # use mold as default linker


# Compile more recent tcmalloc-minimal with clang-15 + -march
RUN git clone --quiet --branch gperftools-2.9.1 --depth 1 https://github.com/gperftools/gperftools
WORKDIR /gperftools
RUN ./autogen.sh
RUN ./configure \
    --enable-minimal \
    --disable-debugalloc \
    --enable-sized-delete \
    --enable-dynamic-sized-delete-support && \
    make -j$(nproc) && \
    make install
WORKDIR /

# install and configure conan
RUN pip3 install conan==1.59.0 && \
    conan user && \
    conan profile new --detect default && \
    conan profile update settings.compiler=clang default && \
    conan profile update settings.compiler.libcxx=libstdc++11 default && \
    conan profile update settings.compiler.cppstd=20 default && \
    conan profile update env.CXXFLAGS="${CXXFLAGS}" default && \
    conan profile update env.CXX="${CXX}" default && \
    conan profile update env.CC="${CC}" default && \
    conan profile update options.boost:extra_b2_flags="cxxflags=\\\"${CXXFLAGS}\\\"" default && \
    conan profile update options.boost:header_only=True default
# note: the conan package for boost (as of 1.79.x/1.80.0) does not build properly on alpine. Therefore, we use only the header_only parts
# todo: remove header_only as soon as build works on alpine

# add conan repositories
RUN conan remote add dice-group https://conan.dice-research.org/artifactory/api/conan/tentris

# build and cache dependencies via conan
WORKDIR /conan_cache
COPY conanfile.txt .
RUN conan install . --build=* --profile default
# import project files
WORKDIR /rdftools
COPY execs execs
COPY cmake cmake
COPY CMakeLists.txt .
COPY VERSION .
COPY conanfile.txt .

##build
WORKDIR /rdftools/build
# todo: should be replaced with toolchain file like https://github.com/ruslo/polly/blob/master/clang-libcxx17-static.cmake
RUN cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DWITH_TCMALLOC=true \
    -DSTATIC=true \
    -DMARCH=${MARCH} \
    ..
RUN make -j $(nproc)

FROM scratch
COPY --from=builder /rdftools/build/execs/deduprdf/deduprdf /rdftools/deduprdf
ENTRYPOINT ["/rdftools/deduprdf"]
