const health = {
  status: "ok",
  service: "esp32-companion",
  api_version: 1,
};

export default {
  fetch(request) {
    const { pathname } = new URL(request.url);
    const headers = { "Cache-Control": "no-store" };

    if (pathname !== "/health") {
      return Response.json({ error: "not_found" }, { status: 404, headers });
    }

    if (request.method !== "GET" && request.method !== "HEAD") {
      return Response.json({ error: "method_not_allowed" }, {
        status: 405,
        headers: { ...headers, Allow: "GET, HEAD" },
      });
    }

    return new Response(request.method === "HEAD" ? null : JSON.stringify(health), {
      headers: { ...headers, "Content-Type": "application/json; charset=utf-8" },
    });
  },
};
