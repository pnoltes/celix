FROM gitpod/workspace-full
RUN sudo apt-get update && \
        sudo apt-get install -yq --no-install-recommends \
          build-essential \
          curl \
          uuid-dev \
          libjansson-dev \
          libcurl4-openssl-dev \
          default-jdk \
          cmake \
          libffi-dev \
          libxml2-dev \
          libczmq-dev \
          libcpputest-dev \
          libtbb-dev
