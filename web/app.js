(function () {
  const scenario = document.getElementById("scenario");
  const form = document.getElementById("form");
  const status = document.getElementById("status");
  const results = document.getElementById("results");
  const method = document.getElementById("method");
  const loadExample = document.getElementById("load-example");

  function num(x, decimals) {
    return Number(x).toFixed(decimals);
  }

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }

  function setStatus(text) {
    if (!text) {
      status.hidden = true;
      status.textContent = "";
      return;
    }
    status.hidden = false;
    status.textContent = text;
  }

  function table(headers, rows, rowClass) {
    const t = el("table");
    const head = el("thead");
    const hr = el("tr");
    headers.forEach(function (h) {
      hr.appendChild(el("th", h.num ? "num" : "", h.text));
    });
    head.appendChild(hr);
    t.appendChild(head);
    const body = el("tbody");
    rows.forEach(function (cells) {
      const tr = el("tr", rowClass);
      cells.forEach(function (c) {
        tr.appendChild(el("td", c.num ? "num" : "", c.text));
      });
      body.appendChild(tr);
    });
    t.appendChild(body);
    return t;
  }

  function section(title, note) {
    const s = el("section");
    s.appendChild(el("h2", "", title));
    if (note) s.appendChild(el("p", "note", note));
    return s;
  }

  function joinPath(ids) {
    return ids.join(" -> ");
  }

  function render(data) {
    results.innerHTML = "";
    results.hidden = false;
    results.classList.remove("visible");
    void results.offsetWidth;
    results.classList.add("visible");

    const sc = data.scenario;
    const port = data.portfolio;
    const used = (port.used || []).map(function (u) {
      return u.id + " " + num(u.used, 1) + " of " + num(u.capacity, 1);
    }).join(", ");

    const funded = section(
      "Recommended portfolio",
      sc.name + ". Scored by " + data.method + ". Total value " + num(port.value, 4) +
        ", cost " + num(port.cost, 0) + " of " + num(port.budget, 0) +
        (used ? ", " + used : "") +
        (port.optimal ? ". Proved optimal." : ".")
    );
    funded.appendChild(table(
      [
        { text: "Project" },
        { text: "Cost", num: true },
        { text: "Duration", num: true },
        { text: "Value", num: true },
        { text: "Requires" }
      ],
      (port.funded || []).map(function (p) {
        return [
          { text: p.id + (p.name ? "  " + p.name : "") },
          { text: num(p.cost, 0), num: true },
          { text: num(p.duration, 1), num: true },
          { text: num(p.value, 4), num: true },
          { text: (p.requires && p.requires.length) ? p.requires.join(", ") : "—" }
        ];
      }),
      "funded"
    ));
    results.appendChild(funded);

    if (data.programme) {
      const path = joinPath(data.programme.critical_path || []);
      const prog = section(
        "Programme schedule",
        "Duration " + num(data.programme.duration, 1) + " periods" +
          (path ? ". Critical path: " + path : "") + "."
      );
      prog.appendChild(table(
        [
          { text: "Project" },
          { text: "Duration", num: true },
          { text: "Early start", num: true },
          { text: "Early finish", num: true },
          { text: "Float", num: true },
          { text: "Critical" }
        ],
        (data.programme.tasks || []).map(function (t) {
          return [
            { text: t.id },
            { text: num(t.duration, 1), num: true },
            { text: num(t.early_start, 1), num: true },
            { text: num(t.early_finish, 1), num: true },
            { text: num(t.float, 1), num: true },
            { text: t.critical ? "yes" : "" }
          ];
        })
      ));
      results.appendChild(prog);
    }

    const rejected = section(
      "Not funded, and why",
      "Each reason is the first binding constraint the engine reports, in the same order as the command line."
    );
    rejected.appendChild(table(
      [
        { text: "Project" },
        { text: "Cost", num: true },
        { text: "Value", num: true },
        { text: "Reason" }
      ],
      (port.rejected || []).map(function (p) {
        return [
          { text: p.id + (p.name ? "  " + p.name : "") },
          { text: num(p.cost, 0), num: true },
          { text: num(p.value, 4), num: true },
          { text: p.reason || "" }
        ];
      }),
      "rejected"
    ));
    results.appendChild(rejected);

    if (data.risk) {
      const p = data.risk.probability_within_budget;
      const risk = section(
        "Risk of the funded portfolio",
        data.risk.samples + " Monte Carlo samples. Probability the total cost stays within the budget of " +
          num(sc.budget, 0) + ": " + num(100 * p, 1) + " per cent."
      );
      risk.appendChild(table(
        [
          { text: "Quantity" },
          { text: "Mean", num: true },
          { text: "Std dev", num: true },
          { text: "p05", num: true },
          { text: "p50", num: true },
          { text: "p95", num: true }
        ],
        [
          [
            { text: "Total cost" },
            { text: num(data.risk.cost.mean, 0), num: true },
            { text: num(data.risk.cost.std_dev, 1), num: true },
            { text: num(data.risk.cost.p05, 0), num: true },
            { text: num(data.risk.cost.p50, 0), num: true },
            { text: num(data.risk.cost.p95, 0), num: true }
          ],
          [
            { text: "Completion" },
            { text: num(data.risk.completion.mean, 2), num: true },
            { text: num(data.risk.completion.std_dev, 2), num: true },
            { text: num(data.risk.completion.p05, 2), num: true },
            { text: num(data.risk.completion.p50, 2), num: true },
            { text: num(data.risk.completion.p95, 2), num: true }
          ]
        ]
      ));
      results.appendChild(risk);
    }

    if (data.rankings) {
      const extra = el("details");
      extra.appendChild(el("summary", "", "AHP against TOPSIS"));
      extra.appendChild(el(
        "p",
        "note",
        "Spearman rank correlation " + num(data.rankings.spearman, 4) +
          ". Projects placed differently: " + num(data.rankings.disagreements, 0) +
          " of " + (data.rankings.projects || []).length + "."
      ));
      extra.appendChild(table(
        [
          { text: "Project" },
          { text: "AHP", num: true },
          { text: "Rank", num: true },
          { text: "TOPSIS", num: true },
          { text: "Rank", num: true }
        ],
        (data.rankings.projects || []).map(function (p) {
          return [
            { text: p.id },
            { text: num(p.ahp, 4), num: true },
            { text: num(p.ahp_rank, 0), num: true },
            { text: num(p.topsis, 4), num: true },
            { text: num(p.topsis_rank, 0), num: true }
          ];
        })
      ));
      results.appendChild(extra);
    }

    results.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  async function loadBundled() {
    setStatus("");
    const response = await fetch("/api/example");
    const text = await response.text();
    if (!response.ok) {
      setStatus("Could not load the bundled scenario.");
      return;
    }
    scenario.value = text;
  }

  form.addEventListener("submit", async function (event) {
    event.preventDefault();
    setStatus("Running the engine…");
    results.hidden = true;
    try {
      const response = await fetch("/api/analyse?method=" + encodeURIComponent(method.value), {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: scenario.value
      });
      const payload = JSON.parse(await response.text());
      if (!response.ok) {
        setStatus(payload.error || "The engine rejected the scenario.");
        return;
      }
      setStatus("");
      render(payload);
    } catch (err) {
      setStatus(err && err.message ? err.message : "The request failed.");
    }
  });

  loadExample.addEventListener("click", function () {
    loadBundled().catch(function (err) {
      setStatus(err && err.message ? err.message : "The request failed.");
    });
  });

  loadBundled().catch(function () {
    setStatus("Could not load the bundled scenario.");
  });
})();
