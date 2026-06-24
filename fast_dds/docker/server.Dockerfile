ARG FASTDDS_BASE_IMAGE=eprosima/vulcanexus:kilted-base
FROM ${FASTDDS_BASE_IMAGE} AS build

ARG CMAKE_BUILD_TYPE=Release
ARG CMAKE_PREFIX_PATH=/opt/ros/kilted
ARG SERVER_INSTALL_PREFIX=/opt/safe-edge/server

WORKDIR /workspace

COPY fast_dds/idl /workspace/fast_dds/idl
COPY fast_dds/server /workspace/fast_dds/server
COPY common_server /workspace/common_server

RUN cmake -S /workspace/fast_dds/server \
    -B /workspace/build/server \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH} \
    -DCMAKE_INSTALL_PREFIX=${SERVER_INSTALL_PREFIX} \
 && cmake --build /workspace/build/server --target install -j"$(nproc)"

FROM ${FASTDDS_BASE_IMAGE} AS runtime

ARG SERVER_INSTALL_PREFIX=/opt/safe-edge/server

ENV LD_LIBRARY_PATH=/opt/ros/kilted/lib
ENV FASTDDS_BUILTIN_TRANSPORTS=UDPv4

COPY --from=build ${SERVER_INSTALL_PREFIX} ${SERVER_INSTALL_PREFIX}

WORKDIR ${SERVER_INSTALL_PREFIX}

ENTRYPOINT ["/opt/safe-edge/server/bin/safe_edge_server"]
