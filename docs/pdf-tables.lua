-- pandoc filter for docs/export-pdf.sh: sizes each column of a wide table by its
-- longest cell (capped), so a narrow "#" column doesn't get the same width as a description.

local CAP = 60   -- longest cell length that still counts, in characters
local MIN = 4    -- narrowest column, in characters

-- A <br> inside a table cell (GitHub renders it) becomes a real line break in the PDF
function RawInline(el)
  if el.format == "html" and el.text:match("^<br%s*/?>$") then
    return pandoc.LineBreak()
  end
end

local function cell_length(cell)
  return utf8.len(pandoc.utils.stringify(cell.contents)) or 0
end

function Table(tbl)
  local ncols = #tbl.colspecs
  -- Only tables that pandoc already gave relative widths (wide source lines)
  for i = 1, ncols do
    if type(tbl.colspecs[i][2]) ~= "number" then
      return nil
    end
  end

  local longest = {}
  for i = 1, ncols do longest[i] = MIN end
  local function scan(rows)
    for _, row in ipairs(rows) do
      for i, cell in ipairs(row.cells) do
        if i <= ncols then
          longest[i] = math.max(longest[i], math.min(cell_length(cell), CAP))
        end
      end
    end
  end
  scan(tbl.head.rows)
  for _, body in ipairs(tbl.bodies) do scan(body.body) end

  local total = 0
  for i = 1, ncols do total = total + longest[i] end
  for i = 1, ncols do
    tbl.colspecs[i] = { tbl.colspecs[i][1], longest[i] / total }
  end
  return tbl
end
