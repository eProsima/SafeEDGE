ARG FASTDDS_BASE_IMAGE=eprosima/vulcanexus:kilted-base
FROM ${FASTDDS_BASE_IMAGE} AS build

ARG CMAKE_BUILD_TYPE=Release
ARG CMAKE_PREFIX_PATH=/opt/ros/kilted
ARG SAFETY_INSTALL_PREFIX=/opt/safe-edge/safety
ARG NON_SAFETY_INSTALL_PREFIX=/opt/safe-edge/non-safety

WORKDIR /workspace

COPY safe_dds/idl /workspace/safe_dds/idl
COPY safe_dds/safety /workspace/safe_dds/safety
COPY safe_dds/non_safety /workspace/safe_dds/non_safety

RUN cmake -S /workspace/safe_dds/safety \
    -B /workspace/build/safety \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH} \
    -DCMAKE_INSTALL_PREFIX=${SAFETY_INSTALL_PREFIX} \
 && cmake --build /workspace/build/safety --target install -j"$(nproc)"

RUN cmake -S /workspace/safe_dds/non_safety \
    -B /workspace/build/non_safety \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
    -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH} \
    -DCMAKE_INSTALL_PREFIX=${NON_SAFETY_INSTALL_PREFIX} \
 && cmake --build /workspace/build/non_safety --target install -j"$(nproc)"

FROM ${FASTDDS_BASE_IMAGE} AS runtime

ARG SAFETY_INSTALL_PREFIX=/opt/safe-edge/safety
ARG NON_SAFETY_INSTALL_PREFIX=/opt/safe-edge/non-safety

ENV LD_LIBRARY_PATH=/opt/ros/kilted/lib
ENV FASTDDS_BUILTIN_TRANSPORTS=UDPv4
ENV SAFE_EDGE_INPUT_FILE=/data/safe-edge-stage2/input.txt

COPY --from=build ${SAFETY_INSTALL_PREFIX} ${SAFETY_INSTALL_PREFIX}
COPY --from=build ${NON_SAFETY_INSTALL_PREFIX} ${NON_SAFETY_INSTALL_PREFIX}
COPY fast_dds/docker/vehicle-entrypoint.sh /opt/safe-edge/vehicle-entrypoint.sh

RUN chmod +x /opt/safe-edge/vehicle-entrypoint.sh \
 && mkdir -p /var/log/safe-edge /data/safe-edge-stage2

WORKDIR /opt/safe-edge

ENTRYPOINT ["/opt/safe-edge/vehicle-entrypoint.sh"]
