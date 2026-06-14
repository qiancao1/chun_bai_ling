const plugin = require('./main.js');
const api = createAPI();
let buffer = Buffer.alloc(0);
const pending = new Map();
let nextId = 1;

function sendResponse(id, result, error) {
    const response = { id, result, error };
    const json = JSON.stringify(response);
    const len = Buffer.byteLength(json, 'utf8');
    const header = Buffer.alloc(4);
    header.writeUInt32BE(len, 0);
    process.stdout.write(Buffer.concat([header, Buffer.from(json, 'utf8')]));
}

function sendEvent(type, data) {
    const event = { type, data };
    const json = JSON.stringify(event);
    const len = Buffer.byteLength(json, 'utf8');
    const header = Buffer.alloc(4);
    header.writeUInt32BE(len, 0);
    process.stdout.write(Buffer.concat([header, Buffer.from(json, 'utf8')]));
}

process.stdin.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (buffer.length >= 4) {
        const len = buffer.readUInt32BE(0);
        if (buffer.length < 4 + len) break;
        const raw = buffer.slice(4, 4 + len);
        const jsonStr = raw.toString('utf8');
        buffer = buffer.slice(4 + len);
        try {
            const msg = JSON.parse(jsonStr);

			if (msg.id !== undefined && pending.has(msg.id)) {
				const { resolve, reject } = pending.get(msg.id);
				pending.delete(msg.id);
				if (msg.error) {
					reject(msg.error);
				} else {
					resolve(msg.result);
				}
			}
            else if (msg.method && msg.id !== undefined) {
                const args = msg.params || [];
                if (typeof plugin[msg.method] === 'function') {
                    Promise.resolve(plugin[msg.method](...args))
                        .then(res => sendResponse(msg.id, res, null))
                        .catch(err => sendResponse(msg.id, null, err.message));
                } else {
                    sendResponse(msg.id, null, `Method ${msg.method} not found`);
                }
            }
			else if (msg.type) {
				// 1. 安全解析事件数据
				let rawData = msg.data;
				let data = {};
				if (typeof rawData === 'string') {
					try {
						data = JSON.parse(rawData);
					} catch (e) {
						console.error(`[Bridge] Failed to parse event data for ${msg.type}:`, e.message);
					}
				} else if (rawData && typeof rawData === 'object') {
					data = rawData;
				}
				// 确保 data 是对象
				if (typeof data !== 'object') data = {};

				const handler = plugin[msg.type];
				// 2. 安全发送 ok（仅当 data 中包含有效的 type）
				const sendOk = () => {
					let type = data.type;
					if (type !== undefined && type >= 0 && type <= 3) {
						api.ok(type, data.d?.group_id, data.d?.id).catch(e => console.error('ok call failed', e));
					}
				};

				if (typeof handler === 'function') {
					// 等待 on_message 执行完成（包括异步操作），然后发送 ok
					Promise.resolve(handler(data))
						.then(() => sendOk())
						.catch(err => {
							console.error(`Error in ${msg.type}:`, err);
							sendOk(); // 错误时也发送 ok（可选）
						});
				} else {
					console.error(`[Bridge] No handler for event: ${msg.type}`);
					sendOk();
				}
			}
        } catch(e) {
            console.error('Bridge parse error:', e.message, 'in', jsonStr);
        }
    }
});

// 创建 API 代理
function createAPI() {
    return new Proxy({}, {
        get: (target, prop) => {
            return (...args) => {
                const id = nextId++;
                const request = { id, method: prop, params: args };
                const json = JSON.stringify(request);
                const len = Buffer.byteLength(json, 'utf8');
                const header = Buffer.alloc(4);
                header.writeUInt32BE(len, 0);
                process.stdout.write(Buffer.concat([header, Buffer.from(json, 'utf8')]));
                return new Promise((resolve, reject) => {
                    pending.set(id, { resolve, reject });
                    setTimeout(() => {
                        if (pending.has(id)) {
                            pending.delete(id);
                            reject(new Error(`API call ${prop} timeout`));
                        }
                    }, 10000);
                });
            };
        }
    });
}

module.exports = { createAPI };