# Official devkitPro image, pinned for both amd64 and arm64 hosts.
FROM devkitpro/devkitarm:20260610@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5

RUN apt-get update \
    && apt-get install -y --no-install-recommends python3-venv python3-pip \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/sm64ds-python
ENV PATH="/opt/sm64ds-python/bin:/opt/sm64ds/build3ds/tools-bin:${PATH}"
COPY docker/requirements.txt /opt/sm64ds/requirements.txt
RUN python3 -m pip install --no-cache-dir -r /opt/sm64ds/requirements.txt

# Build the pinned packaging tools once in the image, using Linux binaries.
COPY build3ds/build_cia_tools.sh /opt/sm64ds/build3ds/build_cia_tools.sh
RUN bash /opt/sm64ds/build3ds/build_cia_tools.sh

WORKDIR /work
CMD ["./build.sh", "--native"]
