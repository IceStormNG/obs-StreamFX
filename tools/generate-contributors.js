const HTTPS = require("https");
const PATH = require("path");
const FS = require("fs/promises");
const CHILD_PROCESS = require("child_process");

function query_git() {
	// git shortlog -sn --all | sed -E "s/^\W+[0-9]+\W+//"
	return (async () => {
		let users = new Map();

		let git = new Promise((resolve, reject) => {
			try {
				const child = CHILD_PROCESS.spawn('git', ['shortlog', '-sn', '--all']);

				let sout = "";
				child.stdout.setEncoding('utf8');
				child.stdout.on('data', (data) => {
					sout += data;
				});

				let serr = "";
				child.stderr.setEncoding('utf8');
				child.stderr.on('data', (data) => {
					serr += data;
				});

				child.on('close', (code) => {
					if (code != 0) {
						reject([code, serr]);
					} else {
						resolve(sout);
					}
				});
			} catch (e) {
				reject(e);
			}
		});

		let result = await git;
		let lines = result.matchAll(/^[\s]+([0-9]+)[\s]+(.+)$/gim);
		for (let line of lines) {
			let name = line[2];
			if (!users.has(name)) {
				users.set(name, `https://github.com/${process.env.GITHUB_REPOSITORY}/graphs/contributors`)
			}
		}

		return users;
	})();
}

function query_crowdin(project_id, project_auth_key) {
	const limit = 100;
	function _query_page(page) {
		return new Promise((resolve, reject) => {
			try {
				let query = {
					protocol: "https:",
					host: "crowdin.com",
					path: `/api/v2/projects/${project_id}/members?limit=${limit}&offset=${page * limit}`,
					method: "GET",
					headers: {
						"accept": "application/json",
						"authorization": `Bearer ${project_auth_key}`
					}
				};
				let req = HTTPS.request(query, (res) => {
					let data = "";
					res.setEncoding('utf8');
					res.on('data', (chunk) => {
						data += chunk;
					});
					res.on('end', () => {
						res.data = data;
						resolve(res);
					});
				});
				req.end();
			} catch (e) {
				reject(e);
			}
		});
	}

	return (async () => {
		let page_is_last = false;
		let page = 0;

		let users = new Map();

		while (!page_is_last) {
			let res = await _query_page(page++);
			if (res.statusCode == 200) {
				let json = JSON.parse(res.data);
				let count = json.data.length;
				for (let user of json.data) {
					// Don't credit blocked users for work not done.
					if (user.data.role == 'blocked') {
						continue;
					}

					let key = (user.data.fullName !== null) && (user.data.fullName !== undefined) && (user.data.fullName.length > 0) ? user.data.fullName : user.data.username;

					users.set(key, `https://crowdin.com/profile/${user.data.username}`);
				}

				page_is_last = (count < limit);
			} else {
				throw new Error(JSON.parse(res.data));
			}
		}

		return users;
	})();
}

function query_github_sponsors(auth_token) {
	const HTTPS = require("https");

	let build_query = function (cursor) {
		if (cursor === undefined) {
			return `query {
	viewer {
		sponsors(after: null, first: 0) {
			totalCount
		}
	}
}`;
		} else {
			return `query {
	viewer {
		sponsors(after: ${typeof (cursor) == "string" ? `"${cursor}"` : "null"}, first: 100) {
			nodes {
				__typename
				... on User {
					resourcePath
					login
					name
				}
				... on Organization {
					resourcePath
					login
					name
				}
			}
			pageInfo {
			  endCursor
			  startCursor
			}
		}
	}
}`;
		}
	}

	let do_query = function (schema) {
		return new Promise((resolve, reject) => {
			let text_query = JSON.stringify({ query: schema });

			let request = HTTPS.request(
				"https://api.github.com/graphql",
				{
					method: 'POST',
					headers: {
						'Content-Type': 'application/json',
						'Accept': 'application/json',
						'Authorization': `bearer ${auth_token}`,
						'User-Agent': 'Node.JS'
					},
				}, (res) => {
					let data = "";
					res.setEncoding('utf8');
					res.on('data', (chunk) => {
						data += chunk;
					});
					res.on('error', () => {
						reject();
					});
					res.on('end', () => {
						res.data = JSON.parse(data);
						resolve(res);
					});
				});
			request.write(text_query);
			request.end();
		})
	}

	return (async () => {
		let users = new Map();
		let total = 0;

		// Find the total amount.
		{
			let query = build_query();
			let response = await do_query(query);
			let data = response.data.data;
			total = data.viewer.sponsors.totalCount;
		}

		// Index by page, in 100 user increments.
		let cursor = null;
		for (let idx = 0, edx = total; idx < edx; idx += 100) {
			let query = build_query(cursor);
			let response = await do_query(query);
			let data = response.data.data;

			// Insert data info Map.
			for (let entry of data.viewer.sponsors.nodes) {
				let name = entry.login;
				if (typeof (entry.name) === "string") {
					name = entry.name;
				}

				users.set(name, `https://github.com${entry.resourcePath}`);
			}

			// Update cursor for further queries.
			cursor = data.viewer.sponsors.pageInfo.endCursor;
		}

		return users;
	})();
}

(async () => {
	let json = {};
	let markdown = `# StreamFX Contributors & Supporters

## Contributors

`;

	// Contributors
	{// - Git
		markdown += `### Code, Media
Thanks go to the following people, who have either wrangled with code or wrangled with image editors while saving often in the hopes of not losing any changes:

`;
		json.contributor = {};
		let info = await query_git();
		let extra = JSON.parse(await FS.readFile(PATH.join(__dirname, "patch-contributors-git.json")));
		for (let key in extra) {
			info.set(key, extra[key]);
		}
		for (let key of Array.from(info.keys()).sort((a, b) => a.localeCompare(b, undefined, {numeric: true, sensitivity: 'base'}))) {
			let value = info.get(key);
			json.contributor[key] = value;
			markdown += `* [${key}](${value})\n`;
		}
		markdown += "\n";
	}

	{// - Crowdin
		markdown += `### Translators
[StreamFX relies on crowd-sourced translations to be available in your language.](https://github.com/Xaymar/obs-StreamFX/issues/36) Much thanks go out to all volunteer translators who have taken some time to submit translations on Crowdin.

`;
		json.translator = {};
		let info = await query_crowdin(process.env.CROWDIN_PROJECTID, process.env.CROWDIN_TOKEN);
		let extra = JSON.parse(await FS.readFile(PATH.join(__dirname, "patch-contributors-git.json")));
		for (let key in extra) {
			info.set(key, extra[key]);
		}
		for (let key of Array.from(info.keys()).sort((a, b) => a.localeCompare(b, undefined, {numeric: true, sensitivity: 'base'}))) {
			let value = info.get(key);
			json.translator[key] = value;
			markdown += `* [${key}](${value})\n`;
		}
		markdown += "\n";
	}

	// Supporters
	markdown += `## Supporters
The StreamFX project relies on generous donations from you through [Patreon](https://patreon.com/xaymar), [GitHub](https://github.com/sponsors/xaymar) or [PayPal](https://paypal.me/xaymar). Huge thanks go out to the following people for supporting StreamFX:

`;
	json.supporter = {}
	{// - GitHub
		markdown += `### GitHub Sponsors
`;
		json.supporter.github = {};
		let info = await query_github_sponsors(process.env.GITHUB_TOKEN);
		let extra = JSON.parse(await FS.readFile(PATH.join(__dirname, "patch-supporters-github.json")));
		for (let key in extra) {
			info.set(key, extra[key]);
		}
		for (let key of Array.from(info.keys()).sort((a, b) => a.localeCompare(b, undefined, {numeric: true, sensitivity: 'base'}))) {
			let value = info.get(key);
			json.supporter.github[key] = value;
			markdown += `* [${key}](${value})\n`;
		}
		markdown += "\n";
	}

	{// - Patreon
		// TODO: How do we OAuth without OAuth?!
		markdown += `### Patreon Patrons
`;
		json.supporter.patreon = {};
		let info = new Map();
		let extra = JSON.parse(await FS.readFile(PATH.join(__dirname, "patch-supporters-patreon.json")));
		for (let key in extra) {
			info.set(key, extra[key]);
		}
		for (let key of Array.from(info.keys()).sort((a, b) => a.localeCompare(b, undefined, {numeric: true, sensitivity: 'base'}))) {
			let value = info.get(key);
			json.supporter.patreon[key] = value;
			markdown += `* [${key}](${value})\n`;
		}
		markdown += "\n";
	}

	// Write Markdown file
	FS.writeFile(process.argv[2], 
		markdown, 
		{
			encoding: 'utf8'
		}
	)

	// Write JSON file
	FS.writeFile(process.argv[3], 
		JSON.stringify(json, undefined, '\t'),
		{
			encoding: 'utf8'
		}
	)
})();
