FROM node:22-alpine

# better-sqlite3 needs a native build
RUN apk add --no-cache python3 make g++

WORKDIR /app
COPY package.json .
RUN npm install --omit=dev

COPY indexer.js .

VOLUME ["/data"]
EXPOSE 3001

CMD ["node", "indexer.js"]
