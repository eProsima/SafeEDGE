ARG FASTDDS_BASE_IMAGE=eprosima/vulcanexus:kilted-base
FROM ${FASTDDS_BASE_IMAGE} AS build

ARG CMAKE_BUILD_TYPE=Release
ARG CMAKE_PREFIX_PATH=/opt/ros/kilted
ARG EDGE_INSTALL_PREFIX=/opt/safe-edge/edge

WORKDIR /workspace

COPY fast_dds/idl /workspace/fast_dds/idl
COPY fast_dds/edge /workspace/fast_dds/edge

RUN cmake -S /workspace/fast_dds/edge \
    -B /workspace/build/edge \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH} \
    -DCMAKE_INSTALL_PREFIX=${EDGE_INSTALL_PREFIX} \
 && cmake --build /workspace/build/edge --target install -j"$(nproc)"

FROM ${FASTDDS_BASE_IMAGE} AS runtime

ARG EDGE_INSTALL_PREFIX=/opt/safe-edge/edge

ENV LD_LIBRARY_PATH=/opt/ros/kilted/lib
ENV FASTDDS_BUILTIN_TRANSPORTS=UDPv4

COPY --from=build ${EDGE_INSTALL_PREFIX} ${EDGE_INSTALL_PREFIX}

WORKDIR ${EDGE_INSTALL_PREFIX}

ENTRYPOINT ["/opt/safe-edge/edge/bin/safe_edge_edge_gateway"]
