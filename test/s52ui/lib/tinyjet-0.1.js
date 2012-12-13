// constants with URIs of XML namespaces
var exports = {};

exports.HTML_NS = 'http://www.w3.org/1999/xhtml';
exports.SVG__NS = 'http://www.w3.org/2000/svg';

//exports.dumpMessage = function(aString)
function _dumpMessage(aString) {
    console.log('tinyjet.js: dumpMessage(): ' + aString);
}

exports.inspect     = function(obj) {
  _dumpMessage("-----------");
  for (prop in obj)
      _dumpMessage(prop + ": " + obj[prop]);
  _dumpMessage("-----------");
}

exports.dumpElement = function(element) {
    var serializer = new XMLSerializer();
    var xml = serializer.serializeToString(element);
    _dumpMessage(xml);
}

function _bindJs(fn, selfObj, var_args)
{
    var context = selfObj || window;

    if (arguments.length > 2) {
        var boundArgs = Array.prototype.slice.call(arguments, 2);
        return function() {
            // Prepend the bound arguments to the current arguments.
            var newArgs = Array.prototype.slice.call(arguments);
            Array.prototype.unshift.apply(newArgs, boundArgs);
            return fn.apply(context, newArgs);
        }
    }
    else {
        return function() {
            return fn.apply(context, arguments);
        }
    }
}

/** @type {!Function} */
function _bindNative(fn, selfObj, var_args)
{
    return fn.call.apply(fn.bind, arguments);
}

exports.bind        = function(fn, selfObj, var_args) {
    if (Function.prototype.bind && Function.prototype.bind.toString().indexOf('native code') != -1) {
        exports.bind = _bindNative;
    } else {
        exports.bind = _bindJs;
    }

    //_dumpMessage('exports.bind:' + arguments);
    //exports.inspect(arguments);

    return exports.bind.apply(null, arguments);
}

/** @constructor */
exports.inherits    = function(childCtor, parentCtor) {
    function tempCtor() {};
    tempCtor.prototype    = parentCtor.prototype;
    childCtor.superClass_ = parentCtor.prototype;
    childCtor.prototype   = new tempCtor();
    childCtor.prototype.constructor = childCtor;
}

exports.clone       = function(obj) {
    var res = {};
    for (var key in obj) {
        res[key] = obj[key];
    }

    return res;
}

exports.extend      = function(target, var_args) {
    var PROTOTYPE_FIELDS_ = ['constructor',
                             'hasOwnProperty',
                             'isPrototypeOf',
                             'propertyIsEnumerable',
                             'toLocaleString',
                             'toString',
                             'valueOf'
                            ];

    for (var i = 1; i < arguments.length; i++) {
        var source = arguments[i];
        for (var key1 in source) {
            //_dumpMessage('key1: ' + key1);
            target[key1] = source[key1];
        }

        for (var j = 0; j < PROTOTYPE_FIELDS_.length; j++) {
            var key2 = PROTOTYPE_FIELDS_[j];
            if (Object.prototype.hasOwnProperty.call(source, key2)) {
                //_dumpMessage('key2: ' + key2 + ', source: ' + source[key2]);
                //_dumpMessage('obj: ' + source + ', prop: ' + key2);
                target[key2] = source[key2];
            }
        }
    }
}

/**
 * Generates an unique identificator for general purposes
 * @return {String} an unique string indentificator
*/
exports.id          = function() {
    exports.id.lastUID++;
    return 'TINYJET_ID_' + exports.id.lastUID.toString();
}
exports.id.lastUID  = 0;

/* not used
exports.getProperty = function(receiver, propertyNameOrClosure) {
    if (!propertyNameOrClosure)
        return object;

    _dumpMessage(propertyNameOrClosure);
    exports.inspect(propertyNameOrClosure);

    if (typeof propertyNameOrClosure == 'function')
        return propertyNameOrClosure(receiver);

    var obj      = receiver;
    var splitted = propertyNameOrClosure.split('.');
    splitted.forEach(function (each) {
                         obj = obj[each];
                     });

    return obj;
}

exports.selectedListItem = function(listboxElement) {
    var collection = listboxElement.attachedObjects['attachedObject'];
    var index      = listboxElement.selectedItem.getAttribute('htmljet:value');

    return collection[parseInt(index, 10)];
}
*/

////////////////////////////////////////////////////////////////////////
//
// Closure registry
//

/**
 * ClosureRegistry is a map where keys are unique strings and values are
 * closures or references to functions.
 * @constructor
*/
window._ClosureRegistry = function() {};
window._ClosureRegistry.lastCID  = 0;
window._ClosureRegistry.registry = {};

/**
 * Adds a closure to the closure registry
 * @param {Function} aClosure
 * @return {String} an assigned identificator
*/
window._ClosureRegistry.add = function(aClosure) {
    window._ClosureRegistry.lastCID++;
    var cid = 'closure' + window._ClosureRegistry.lastCID.toString();
    window._ClosureRegistry.registry[cid] = aClosure;

    return cid;
}


///////////////////////////////////////////////////////
//
// AbstractCanvas
//

exports.AbstractCanvas = function(parentCanvas, aComponent) {
    this.nodes     = parentCanvas ? parentCanvas.nodes : new Array();
    this.component = aComponent;

    //this.automaticProperties = false;
}

exports.AbstractCanvas.prototype.roots       = function() {
    return this.nodes.filter(function(each) {
                                 return each.parentNode == undefined;
                             });
}

exports.AbstractCanvas.prototype.HTML        = function() {
    return new exports.HTMLCanvas(this, this.component);
}

exports.AbstractCanvas.prototype.SVG         = function() {
    return new exports.SVGCanvas(this, this.component);
}

exports.AbstractCanvas.prototype.insertNodes = function(nodes) {
    nodes.isElementsCollection = true;
    nodes.forEach(function(each) {
                      this.nodes.push(each)
                  },
                  this);

    return nodes;
}

exports.AbstractCanvas.prototype.insert      = function(aClosure, arg) {
    var embeddedCanvas = new this.constructor(null, this.component);
    var boundClosure   = exports.bind(aClosure, this.component);
    boundClosure(embeddedCanvas, arg);

    var roots = embeddedCanvas.roots();
    roots.isElementsCollection = true;
    roots.forEach(function(each) {
                      this.nodes.push(each);
                  },
                  this);

    return roots;
}

exports.AbstractCanvas.prototype.collect     = function(aCollection, aClosure) {
    var boundClosure = exports.bind(aClosure, this.component);
    return this.insert(function(_html) {
                           aCollection.forEach(function(item, index) {
													boundClosure(_html, item, index);
                                               });
                       });
}

exports.AbstractCanvas.prototype.sectionBeg  = function(id) {
    //var oldSetting = this.automaticProperties;
    //this.automaticProperties = false;
    var result = this.p({hidden: true, id: '__beg_'+id });
    //this.automaticProperties = oldSetting;

    //_dumpMessage('sectionBeg:' + id);

    return result;
}

exports.AbstractCanvas.prototype.sectionEnd  = function(id) {
    //var oldSetting = this.automaticProperties;
    //this.automaticProperties = false;
    var result = this.p({hidden: true, id: '__end_'+id });
    //this.automaticProperties = oldSetting;

    //_dumpMessage('sectionEnd:' + id);

    return result;
}


///////////////////////////////////////////////////////////////////////////
//
// HTMLCanvas
//

exports.HTMLCanvas = function(parentCanvas, aComponent) {
    exports.AbstractCanvas.call(this, parentCanvas, aComponent);
    this.nodes = parentCanvas ? parentCanvas.nodes : [];
}
exports.inherits(exports.HTMLCanvas, exports.AbstractCanvas);

exports.HTMLCanvas.prototype.tags =
[
 'a', 'abbr', 'acronym', 'address', 'applet', 'area', 'article', 'aside', 'audio',
 'b', 'base', 'basefont', 'bdo', 'big', 'blockquote', 'body', 'br', 'button',
 'canvas', 'caption', 'center', 'cite', 'code', 'col', 'colgroup', 'command',
 'datalist', 'dd', 'del', 'details', 'dfn', 'dir', 'div', 'dl', 'dt',
 'em', 'embed', 'fieldset', 'figcaption', 'figure', 'font', 'footer', 'form', 'frame', 'frameset',
 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'head', 'header', 'hgroup', 'hr', 'html',
 'i', 'iframe', 'img', 'input', 'ins',
 'keygen', 'kbd',
 'label', 'legend', 'li', 'link',
 'map', 'mark', 'menu', 'meta', 'meter',
 'nav', 'noframes', 'noscript',
 'object', 'ol', 'optgroup', 'option', 'output',
 'p', 'param', 'pre', 'progress',
 'q',
 'rp', 'rt', 'ruby',
 's', 'samp', 'script', 'section', 'select', 'small', 'source', 'span', 'strike', 'strong', 'style', 'sub', 'summary', 'sup',
 'table', 'tbody', 'td', 'textarea', 'tfoot', 'th', 'thead', 'time', 'title', 'tr', 'tt',
 'u', 'ul',
 'var', 'video',
 'wbr',
 'xmp'
];


///////////////////////////////////////////////////////////////////////////////
//
// SVGCanvas
//

exports.SVGCanvas = function(parentCanvas, aComponent) {
    exports.AbstractCanvas.call(this, parentCanvas, aComponent);
    this.nodes = parentCanvas ? parentCanvas.nodes : [];
}
exports.inherits(exports.SVGCanvas, exports.AbstractCanvas);

exports.SVGCanvas.prototype.tags =
[
 'a', 'altGlyph', 'altGlyphDef', 'altGlyphItem', 'animate', 'animateColor', 'animateMotion', 'animateTransform',
 'circle', 'clipPath', 'color-profile', 'cursor',
 'definition-src', 'defs', 'desc',
 'ellipse',

 'feBlend',
 'feColorMatrix', 'feComponentTransfer', 'feComposite', 'feConvolveMatrix',
 'feDiffuseLighting', 'feDisplacementMap', 'feDistantLight',
 'feFlood', 'feFuncA', 'feFuncB', 'feFuncG', 'feFuncR',
 'feGaussianBlur',
 'feImage',
 'feMerge', 'feMergeNode', 'feMorphology',
 'feOffset',
 'fePointLight',
 'feSpecularLighting', 'feSpotLight',
 'feTile', 'feTurbulence',

 'filter', 'font', 'font-face', 'font-face-format', 'font-face-name', 'font-face-src', 'font-face-uri', 'foreignObject',
 'g', 'glyph', 'glyphRef',
 'hkern',
 'image',
 'line', 'linearGradient',
 'marker', 'mask', 'metadata', 'missing-glyph', 'mpath',
 'path', 'pattern', 'polygon', 'polyline',
 'radialGradient', 'rect',
 'script', 'set', 'stop', 'style', 'svg', 'switch', 'symbol',
 'text', 'textPath', 'title', 'tref', 'tspan',
 'use',
 'view'
];


///////////////////////////////////////////////////////////////////
//
// Component
//

exports.Component = function(aWindow) {
    this.uid                = exports.id();
    this.registeredClosures = [];
    this.window             = aWindow;
    this.localesPackageName = undefined;
}

exports.Component.prototype.children      = function() {
    return [];
}

exports.Component.prototype.clearClosures = function() {
    this.registeredClosures.forEach(function(each) {
                                        delete window.ClosureRegistry.registry.each;
                                    });
    this.registeredClosures = [];
}

exports.Component.prototype.setWindow     = function(aWindow) {
    this.window = aWindow;
    this.children().forEach(function (each) {
                                each.setWindow(aWindow);
                            });
}

exports.Component.prototype.ID            = function(aString) {
    if (!this._ids)
        this._ids = {};

    var result = this._ids[aString];
    if (result != undefined)
        return result;

    result = exports.id();
    this._ids[aString] = result;

    return result;
}

exports.Component.prototype.$C            = function(aClosure) {
    var boundClosure = exports.bind(aClosure, this);
    var closureId    = window._ClosureRegistry.add(boundClosure);
    this.registeredClosures.push(closureId);

    return
        'if (typeof event == \'undefined\')                                           \
            (window._ClosureRegistry.registry["'+closureId+'"])(this, undefined);     \
        else                                                                          \
            (window._ClosureRegistry.registry["'+closureId+'"])(this, event)';
}

exports.Component.prototype.$             = function(element) {
    if (arguments.length > 1) {
        for (var i = 0, elements = [], length = arguments.length; i < length; i++)
            elements.push(this.$(arguments[i]));

        return elements;
    }

    if (typeof element == 'string')
        element = this.window.document.getElementById(element);

    return element;
}

// main hook to render stuff
exports.Component.prototype.render        = function(){}

// this is a hook to add stuff after render() (I think!)
exports.Component.prototype.finishRendering = function(){}
exports.Component.prototype.finishRenderingWithChildren = function() {
  this.finishRendering();
  this.children().forEach(function (each) {
                              each.finishRenderingWithChildren();
                          });
}

exports.Component.prototype.rendered      = function() {
    this.clearClosures();

    var html = new exports.HTMLCanvas(null, this);
    html.sectionBeg(this.uid);
    this.render(html);
    html.sectionEnd(this.uid);

    var roots = html.roots();
    roots.isElementsCollection = true;

    return roots;
}

exports.Component.prototype.refresh       = function() {
    var roots = this.rendered();
    _replaceComponentInDocument(this.window.document, this.uid, roots);

    this.finishRenderingWithChildren();
}

exports.Component.prototype.replace       = function(oldComponent) {
    var roots = this.rendered();
    oldComponent.clearClosures();
    _replaceComponentInDocument(this.window.document, oldComponent.id, roots);

    this.finishRenderingWithChildren();
}

exports.Component.prototype.remove        = function() {
    this.clearClosures();
    _replaceComponentInDocument(this.uid, []);
}

exports.Component.prototype.refreshSection = function(id, aClosure ) {
    var boundClosure = exports.bind(aClosure, this);
    var html         = new exports.HTMLCanvas(null, this);

    html.sectionBeg(id);
    boundClosure(html);
    html.sectionEnd(id);

    var roots = html.roots();
    roots.isElementsCollection = true;
    _replaceComponentInDocument(this.window.document, id, roots);
}

exports.Component.prototype.beMainWindowComponent = function() {
    this.uid = 'mainWindowComponent';
    this.refresh();
}


//////////////////////////////////////////////////////////////////
//
// Window
//

// replacement for all
exports.Window = function() {}
//exports.Window.prototype.rootNode = function() {
//    return this.document.getElementsByTagName('window')[0];
//}

/* not used
exports.Window = function(component, title) {
    this.onWindowOpened = function(newWin) {
        exports.extend(newWin, exports.Window.prototype);

        newWin._ClosureRegistry = window._ClosureRegistry;

        //newWin._setTitle(title);
        component.setWindow(newWin);

        var roots = component.rendered();
        _replaceComponentInDocument(newWin.document, 'windowComponent', roots);
        component.finishRenderingWithChildren();
    }
}
//*/

// not usefull
//exports.Window.prototype._setTitle = function(aString) {
//    this.document.title = aString;
//}


//////////////////////////////////////////////////////////////////////
//
// Generate renderer classes
//

exports.isAttributes     = function(attrs) {
    return (typeof attrs == 'object')
        && !(attrs instanceof HTMLElement)
        && !(attrs instanceof SVGElement)
        && !(attrs instanceof Text)
        && !(attrs instanceof Array)
        && !(attrs.isElementsCollection);
}

exports.isElement        = function(anObject) {
    return anObject instanceof Element
        || anObject instanceof Text
        || anObject instanceof HTMLElement
        || anObject instanceof SVGElement
}

exports.processAttribute = function(aNode, key, value, aCanvas) {
    /*
    if (key == 'returncommand')   {
        key = 'onkeypress';
        var oldValue = value;
        value = function(element, event) {
            if (event.keyCode == KeyEvent.DOM_VK_RETURN)
                this.$(oldValue).doCommand()
        }
    }

    if (key == 'link') {
        var currentValue;

        var splitted = value[1].split('.');
        var currentValue = value[0];
        splitted.forEach(function (each) {
                             currentValue = currentValue[each];
                         });

        var safeCurrentValue = (currentValue == undefined || currentValue == null) ? '' : currentValue;

        aNode.setAttribute('htmljet:name', value[1]);
        switch (aNode.tagName) {
        	case 'textbox'    : aNode.setAttribute('value',   safeCurrentValue); break;
        	case 'checkbox'   : aNode.setAttribute('checked', currentValue);     break;
        	case 'colorpicker': aNode.setAttribute('color',   currentValue);     break;
        	default: aNode.setAttribute('value', currentValue);
        }

        aNode.setFormReceiver(value);

        return;
    }

    //if ((key == 'bind') || (key == 'id' && aCanvas.automaticProperties)) {
    //if ((key == 'bind') || (key == 'id')) {
    if (key == 'bind') {
        aNode.bindProperty(aCanvas.component, value);
        aCanvas.component[value] = aNode;
    }
    */

    var val = value;
    if (typeof val == 'function')
        val = aCanvas.component.$C(val);

    aNode.setAttribute(key, val);
}

function _attachObject(tag, propertyName, object)
{
    if (!tag.getAttribute('tinyjet:attachedObjects'))
         tag.setAttribute('tinyjet:attachedObjects', exports.id());

    if (!tag.attachedObjects)
         tag.attachedObjects = {};

    tag.attachedObjects[propertyName] = object;

    return tag;
}

function _collectAttachedObjects(node, objects)
{
    var collectedObjects = objects ? objects : {};

    var objId = node.getAttribute('tinyjet:attachedObjects');
    if (objId)
        collectedObjects[objId] = node.attachedObjects;

    var child = node.firstChild;
    while (child) {
        if (child.nodeType === 1)
            _collectAttachedObjects(child, collectedObjects);

        child = child.nextSibling;
    }

    return collectedObjects;
}

function _setAttachedObjects(node, objects)
{
    var objId = node.getAttribute('tinyjet:attachedObjects');
    if (objId) {
        node.attachedObjects = objects[objId];
        var propertyBinding = node.attachedObjects['propertyBinding'];
        if (propertyBinding)
            propertyBinding.object[propertyBinding.property] = node;
    }

    var child = node.firstChild;
    while (child) {
        if (child.nodeType === 1)
            _setAttachedObjects(child, objects);

        child = child.nextSibling;
    }
}

function _replaceComponentInDocument(aDocument, id, newNodes) {
    var begNode = aDocument.getElementById('__beg_' + id);
    var endNode = aDocument.getElementById('__end_' + id);

    var currentNode = begNode;
    var parentNode  = currentNode.parentNode;

    while (currentNode.nextSibling && (currentNode.nextSibling != endNode))
        parentNode.removeChild(currentNode.nextSibling);

    newNodes.forEach(function(node) {
                         var clonedNode  = node.cloneNode(true);
                         var attachments = _collectAttachedObjects(node);
                         _setAttachedObjects(clonedNode, attachments);
                         parentNode.insertBefore(clonedNode, endNode);
                     });

    begNode.parentNode.removeChild(begNode);
    endNode.parentNode.removeChild(endNode);
}

/* SD - not used
//var replaceBodyInDocument = function(aDocument, newNodes) {
function _replaceBodyInDocument(aDocument, newNodes)
{
    newNodes.forEach(function(node) {
                         var clonedNode  = node.cloneNode(true);
                         var attachments = _collectAttachedObjects(node);
                         _setAttachedObjects(clonedNode, attachments);
                         aDocument.body.appendChild(clonedNode);
                     });
}
*/

exports.makeTagHelper  = function(namespace, prefix, tagName) {
    return function(attrs) {
        var node = document.createElementNS(namespace, prefix + tagName);

        var firstArgIsAttributes = exports.isAttributes(attrs);
        if (firstArgIsAttributes) {
            for (var key in attrs) {
                exports.processAttribute(node, key, attrs[key], this);
            }
        }

        var startIndex = firstArgIsAttributes ? 1 : 0;
        for (var i = startIndex; i < arguments.length; i++) {
            var arg = arguments[i];

            if (typeof arg == 'string' || arg == undefined)
                arg = document.createTextNode(arg || '');

            if (exports.isElement(arg))
                arg.parent = node;

            if (arg.isElementsCollection) {
                for (var j = 0; j < arg.length; j++) {
                    if (arg[j])
                        node.appendChild(arg[j]);
                }
            } else
                node.appendChild(arg);
        }

        this.nodes.push(node);

        return node;
    }
}

exports.makeTagHelpers = function(canvasClass, namespace, prefix) {
    var tags = canvasClass.prototype.tags;
    for (var i = tags.length - 1; i >= 0; i--) {
        var tagName = tags[i];
        canvasClass.prototype[tagName] = exports.makeTagHelper(namespace, prefix, tagName)
    }
}

exports.makeTagHelpers(exports.HTMLCanvas, exports.HTML_NS, 'html:');
exports.makeTagHelpers(exports.SVGCanvas,  exports.SVG__NS, 'svg:');

exports.extend(
               Element.prototype,
               {
               on: function(obj, property) {
                       var currentValue = obj;


                       //var splitted = property.split('.');
                       //splitted.forEach(
                       //                 function (each) {
                       //                     currentValue = currentValue[each];
                       //                 });


                       this.setAttribute('tinyjet:name', property);

                       //switch (this.tagName) {
                       //case 'checkbox'   : this.setAttribute('checked', currentValue); break;
                       //case 'colorpicker': this.setAttribute('color',   currentValue); break;
                       //default: this.setAttribute('value', currentValue)
                       //}
                       this.setAttribute('value', currentValue)

                       this.setFormReceiver([obj, property]);

                       return this;
                   },

               attach: function(obj, propertyName) {
                       var property = propertyName ? propertyName : 'attachedObject';
                       return _attachObject(this, property, obj);
                   },

               bindProperty: function(obj, propertyName) {
                       return _attachObject(this, 'propertyBinding', {object: obj, property: propertyName});
                   },

               setFormReceiver: function(obj) {
                       return _attachObject(this, 'formReceiver', obj);
                   }
               }
              );

function _onMainWindowOpened(loadEvt) {
    exports.extend(window, exports.Window.prototype);
    exports.Window.main = window;

    _main();
}
window.addEventListener('load', _onMainWindowOpened, false);
